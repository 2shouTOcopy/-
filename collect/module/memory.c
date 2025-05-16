/**
 * collectd - src/memory.c
 * Copyright (C) 2005-2014  Florian octo Forster
 * Copyright (C) 2009       Simon Kuhnle
 * Copyright (C) 2009       Manuel Sanmartin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 * Florian octo Forster <octo at collectd.org>
 * Simon Kuhnle <simon at blarzwurst.de>
 * Manuel Sanmartin
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <stdio.h>  // For FILE, fopen, fgets, fclose
#include <string.h> // For strncasecmp, strsplit
#include <stdlib.h> // For atof

/* KERNEL_LINUX */
/* no global variables */
/* #endif KERNEL_LINUX */

static bool values_absolute = true;
static bool values_percentage;

static int memory_config(oconfig_item_t *ci) /* {{{ */
{
    for (int i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        if (strcasecmp("ValuesAbsolute", child->key) == 0)
            cf_util_get_boolean(child, &values_absolute);
        else if (strcasecmp("ValuesPercentage", child->key) == 0)
            cf_util_get_boolean(child, &values_percentage);
        else
            ERROR("memory plugin: Invalid configuration option: "
                  "\"%s\".",
                  child->key);
    }

    return 0;
} /* }}} int memory_config */

static int memory_init(void)
{
    /* #if KERNEL_LINUX */
    /* no init stuff */
    /* #endif KERNEL_LINUX */
    return 0;
} /* int memory_init */

#define MEMORY_SUBMIT(...)                                                           \
    do                                                                               \
    {                                                                                \
        if (values_absolute)                                                         \
            plugin_dispatch_multivalue(vl, false, DS_TYPE_GAUGE, __VA_ARGS__, NULL); \
        if (values_percentage)                                                       \
            plugin_dispatch_multivalue(vl, true, DS_TYPE_GAUGE, __VA_ARGS__, NULL);  \
    } while (0)

#if KERNEL_LINUX
static void memory_submit_available(gauge_t value)
{
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &(value_t){.gauge = value};
    vl.values_len = 1;

    sstrncpy(vl.plugin, "memory", sizeof(vl.plugin));
    sstrncpy(vl.type, "memory", sizeof(vl.type));
    sstrncpy(vl.type_instance, "available", sizeof(vl.type_instance));

    plugin_dispatch_values(&vl);
}
#endif

static int memory_read_internal(value_list_t *vl)
{
    /* #if KERNEL_LINUX */
    FILE *fh;
    char buffer[1024];

    char *fields[8];
    int numfields;

    bool mem_available_info = false;
    bool detailed_slab_info = false;

    gauge_t mem_total = 0;
    gauge_t mem_used = 0;
    gauge_t mem_buffered = 0;
    gauge_t mem_cached = 0;
    gauge_t mem_free = 0;
    gauge_t mem_available = 0;
    gauge_t mem_slab_total = 0;
    gauge_t mem_slab_reclaimable = 0;
    gauge_t mem_slab_unreclaimable = 0;

    if ((fh = fopen("/proc/meminfo", "r")) == NULL)
    {
        WARNING("memory: fopen: %s", STRERRNO);
        return -1;
    }

    while (fgets(buffer, sizeof(buffer), fh) != NULL)
    {
        gauge_t *val = NULL;

        if (strncasecmp(buffer, "MemTotal:", 9) == 0)
            val = &mem_total;
        else if (strncasecmp(buffer, "MemFree:", 8) == 0)
            val = &mem_free;
        else if (strncasecmp(buffer, "Buffers:", 8) == 0)
            val = &mem_buffered;
        else if (strncasecmp(buffer, "Cached:", 7) == 0)
            val = &mem_cached;
        else if (strncasecmp(buffer, "Slab:", 5) == 0)
            val = &mem_slab_total;
        else if (strncasecmp(buffer, "SReclaimable:", 13) == 0)
        {
            val = &mem_slab_reclaimable;
            detailed_slab_info = true;
        }
        else if (strncasecmp(buffer, "SUnreclaim:", 11) == 0)
        {
            val = &mem_slab_unreclaimable;
            detailed_slab_info = true;
        }
        else if (strncasecmp(buffer, "MemAvailable:", 13) == 0)
        {
            val = &mem_available;
            mem_available_info = true;
        }
        else
            continue;

        numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
        if (numfields < 2)
            continue;

        *val = 1024.0 * atof(fields[1]);
    }

    if (fclose(fh))
    {
        WARNING("memory: fclose: %s", STRERRNO);
    }

    if (mem_total < (mem_free + mem_buffered + mem_cached + mem_slab_total))
    {
        // If detailed_slab_info is false, mem_slab_total is used.
        // If detailed_slab_info is true, mem_slab_reclaimable is used in the mem_used calculation later,
        // but the raw mem_slab_total might still be relevant for this check.
        // However, the original logic is preserved.
        if (!detailed_slab_info && (mem_total < (mem_free + mem_buffered + mem_cached + mem_slab_total)))
        {
            return -1;
        }
        else if (detailed_slab_info && (mem_total < (mem_free + mem_buffered + mem_cached + mem_slab_reclaimable + mem_slab_unreclaimable)))
        {
            // SReclaimable + SUnreclaimable should ideally be close to Slab (mem_slab_total)
            // This check needs to be robust. The original check was:
            // if (mem_total < (mem_free + mem_buffered + mem_cached + mem_slab_total)) return -1;
            // This implies that even with detailed_slab_info, mem_slab_total should be somewhat consistent.
            // For now, sticking to a simplified version of the original check is safer if we don't have SUnreclaim in the sum.
            // Let's use the more encompassing check from original code.
            if (mem_total < (mem_free + mem_buffered + mem_cached + mem_slab_total))
                return -1; // This was the original single check.
        }
    }

    if (detailed_slab_info)
        mem_used = mem_total -
                   (mem_free + mem_buffered + mem_cached + mem_slab_reclaimable);
    else
        mem_used =
            mem_total - (mem_free + mem_buffered + mem_cached + mem_slab_total);

    /* SReclaimable and SUnreclaim were introduced in kernel 2.6.19
     * They sum up to the value of Slab, which is available on older & newer
     * kernels. So SReclaimable/SUnreclaim are submitted if available, and Slab
     * if not. */
    if (detailed_slab_info)
        MEMORY_SUBMIT("used", mem_used, "buffered", mem_buffered, "cached",
                      mem_cached, "free", mem_free, "slab_unrecl",
                      mem_slab_unreclaimable, "slab_recl", mem_slab_reclaimable);
    else
        MEMORY_SUBMIT("used", mem_used, "buffered", mem_buffered, "cached",
                      mem_cached, "free", mem_free, "slab", mem_slab_total);

    if (mem_available_info)
        memory_submit_available(mem_available);
    /* #endif KERNEL_LINUX */

    return 0;
} /* }}} int memory_read_internal */

static int memory_read(void) /* {{{ */
{
    value_t v[1];
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = v;
    vl.values_len = STATIC_ARRAY_SIZE(v);
    sstrncpy(vl.plugin, "memory", sizeof(vl.plugin));
    sstrncpy(vl.type, "memory", sizeof(vl.type));
    vl.time = cdtime();

    return memory_read_internal(&vl);
} /* }}} int memory_read */

void module_register(void)
{
    plugin_register_complex_config("memory", memory_config);
    plugin_register_init("memory", memory_init);
    plugin_register_read("memory", memory_read);
} /* void module_register */
