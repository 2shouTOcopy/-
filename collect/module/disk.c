/**
 * collectd - src/disk.c
 * Copyright (C) 2005-2012  Florian octo Forster
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
 * Manuel Sanmartin
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#include <stdio.h>  // For FILE, fopen, fgets, fclose
#include <string.h> // For strcasecmp, strdup, strcmp, strsplit
#include <stdlib.h> // For atoll, atof, calloc, free
#include <math.h>   // For NAN

#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

/* KERNEL_LINUX */
typedef struct diskstats
{
    char *name;

    /* This overflows in roughly 1361 years */
    unsigned int poll_count;

    derive_t read_sectors;
    derive_t write_sectors;

    derive_t read_bytes;
    derive_t write_bytes;

    derive_t read_ops;
    derive_t write_ops;
    derive_t read_time;
    derive_t write_time;

    derive_t avg_read_time;
    derive_t avg_write_time;

    derive_t io_time;

    bool has_merged;
    bool has_in_progress;
    bool has_io_time;
    bool has_discard;
    bool has_flush;

    struct diskstats *next;
} diskstats_t;

static diskstats_t *disklist;

static bool report_discard;
static bool report_flush;
/* #endif KERNEL_LINUX */

#if HAVE_LIBUDEV_H
#include <libudev.h>

static char *conf_udev_name_attr;
static struct udev *handle_udev;
#endif

static const char *config_keys[] = {"Disk", "UseBSDName", // UseBSDName will be handled to show warning
                                    "IgnoreSelected", "UdevNameAttr",
                                    "ReportDiscard", "ReportFlush"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist;

static int disk_config(const char *key, const char *value)
{
    if (ignorelist == NULL)
        ignorelist = ignorelist_create(/* invert = */ 1);
    if (ignorelist == NULL)
        return 1;

    if (strcasecmp("Disk", key) == 0)
    {
        ignorelist_add(ignorelist, value);
    }
    else if (strcasecmp("IgnoreSelected", key) == 0)
    {
        int invert = 1;
        if (IS_TRUE(value))
            invert = 0;
        ignorelist_set_invert(ignorelist, invert);
    }
    else if (strcasecmp("UseBSDName", key) == 0)
    {
        WARNING("disk plugin: The \"UseBSDName\" option is only supported "
                "on Mach / Mac OS X and will be ignored on Linux.");
    }
    else if (strcasecmp("UdevNameAttr", key) == 0)
    {
#if HAVE_LIBUDEV_H
        if (conf_udev_name_attr != NULL)
        {
            free(conf_udev_name_attr);
            conf_udev_name_attr = NULL;
        }
        if ((conf_udev_name_attr = strdup(value)) == NULL)
            return 1;
#else
        WARNING("disk plugin: The \"UdevNameAttr\" option is only supported "
                "if collectd is built with libudev support. This will be ignored.");
#endif
    }
    else if (strcasecmp("ReportDiscard", key) == 0)
    {
        report_discard = IS_TRUE(value);
    }
    else if (strcasecmp("ReportFlush", key) == 0)
    {
        report_flush = IS_TRUE(value);
    }
    else
    {
        return -1;
    }

    return 0;
} /* int disk_config */

static int disk_init(void)
{
/* KERNEL_LINUX */
#if HAVE_LIBUDEV_H
    if (conf_udev_name_attr != NULL)
    {
        handle_udev = udev_new();
        if (handle_udev == NULL)
        {
            ERROR("disk plugin: udev_new() failed!");
            return -1;
        }
    }
#endif /* HAVE_LIBUDEV_H */
    return 0;
} /* int disk_init */

static int disk_shutdown(void)
{
/* KERNEL_LINUX */
#if HAVE_LIBUDEV_H
    if (handle_udev != NULL)
    {
        udev_unref(handle_udev);
        handle_udev = NULL; // Good practice to NULL after unref/free
    }
    // Free disklist
    diskstats_t *ds = disklist;
    while (ds != NULL)
    {
        diskstats_t *next_ds = ds->next;
        free(ds->name);
        free(ds);
        ds = next_ds;
    }
    disklist = NULL;

    sfree(conf_udev_name_attr); // Free udev name attribute string

#endif /* HAVE_LIBUDEV_H */
    if (ignorelist != NULL)
    {
        ignorelist_destroy(ignorelist);
        ignorelist = NULL;
    }
    return 0;
} /* int disk_shutdown */

#if KERNEL_LINUX
static void disk_submit_single(const char *plugin_instance, const char *type,
                               derive_t value)
{
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &(value_t){.derive = value};
    vl.values_len = 1;
    sstrncpy(vl.plugin, "disk", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, type, sizeof(vl.type));

    plugin_dispatch_values(&vl);
} /* void disk_submit_single */
#endif

static void disk_submit(const char *plugin_instance, const char *type,
                        derive_t read, derive_t write)
{
    value_list_t vl = VALUE_LIST_INIT;
    value_t values[] = {
        {.derive = read},
        {.derive = write},
    };

    vl.values = values;
    vl.values_len = STATIC_ARRAY_SIZE(values);
    sstrncpy(vl.plugin, "disk", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, type, sizeof(vl.type));

    plugin_dispatch_values(&vl);
} /* void disk_submit */

#if KERNEL_LINUX
static void submit_io_time(char const *plugin_instance, derive_t io_time,
                           derive_t weighted_time)
{
    value_list_t vl = VALUE_LIST_INIT;
    value_t values[] = {
        {.derive = io_time},
        {.derive = weighted_time},
    };

    vl.values = values;
    vl.values_len = STATIC_ARRAY_SIZE(values);
    sstrncpy(vl.plugin, "disk", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, "disk_io_time", sizeof(vl.type));

    plugin_dispatch_values(&vl);
} /* void submit_io_time */

static void submit_in_progress(char const *disk_name, gauge_t in_progress)
{
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &(value_t){.gauge = in_progress};
    vl.values_len = 1;
    sstrncpy(vl.plugin, "disk", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, disk_name, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, "pending_operations", sizeof(vl.type));

    plugin_dispatch_values(&vl);
}

static void submit_utilization(char const *disk_name, derive_t delta_time)
{
    value_t v;
    value_list_t vl = VALUE_LIST_INIT;

    long interval = CDTIME_T_TO_MS(plugin_get_interval());
    if (interval == 0)
    {
        DEBUG("disk plugin: got zero plugin interval");
        // Avoid division by zero if interval is 0
        v.gauge = NAN;
    }
    else
    {
        v.gauge = ((delta_time / (double)interval) * 100.0);
    }

    vl.values = &v;
    vl.values_len = 1;
    sstrncpy(vl.plugin, "disk", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, disk_name, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, "percent", sizeof(vl.type));
    sstrncpy(vl.type_instance, "utilization", sizeof(vl.type_instance));

    plugin_dispatch_values(&vl);
}

static counter_t disk_calc_time_incr(counter_t delta_time,
                                     counter_t delta_ops)
{
    double interval = CDTIME_T_TO_DOUBLE(plugin_get_interval());
    if (delta_ops == 0)
        return 0; // Avoid division by zero
    double avg_time = ((double)delta_time) / ((double)delta_ops);
    double avg_time_incr = interval * avg_time;

    return (counter_t)(avg_time_incr + .5);
}
#endif

#if HAVE_LIBUDEV_H
/**
 * Attempt to provide an rename disk instance from an assigned udev attribute.
 *
 * On success, it returns a strduped char* to the desired attribute value.
 * Otherwise it returns NULL.
 */

static char *disk_udev_attr_name(struct udev *udev, char *disk_name,
                                 const char *attr)
{
    struct udev_device *dev;
    const char *prop;
    char *output = NULL;

    if (udev == NULL || disk_name == NULL || attr == NULL)
    {
        return NULL;
    }

    dev = udev_device_new_from_subsystem_sysname(udev, "block", disk_name);
    if (dev != NULL)
    {
        prop = udev_device_get_property_value(dev, attr);
        if (prop)
        {
            output = strdup(prop);
            DEBUG("disk plugin: renaming %s => %s", disk_name, output);
        }
        udev_device_unref(dev);
    }
    return output;
}
#endif

static int disk_read(void)
{
    /* KERNEL_LINUX */
    FILE *fh;
    char buffer[1024];

    char *fields[32];
    static unsigned int poll_count = 0;

    derive_t read_sectors = 0;
    derive_t write_sectors = 0;

    derive_t read_ops = 0;
    derive_t read_merged = 0;
    derive_t read_time = 0;
    derive_t write_ops = 0;
    derive_t write_merged = 0;
    derive_t write_time = 0;
    gauge_t in_progress = NAN;
    derive_t io_time = 0;
    derive_t weighted_time = 0;
    derive_t diff_io_time = 0;
    derive_t discard_ops = 0;
    derive_t discard_merged = 0;
    derive_t discard_sectors = 0;
    derive_t discard_time = 0;
    derive_t flush_ops = 0;
    derive_t flush_time = 0;
    int is_disk = 0;

    diskstats_t *ds, *pre_ds;

    /* https://www.kernel.org/doc/Documentation/ABI/testing/procfs-diskstats */
    if ((fh = fopen("/proc/diskstats", "r")) == NULL)
    {
        ERROR("disk plugin: fopen(\"/proc/diskstats\"): %s", STRERRNO);
        return -1;
    }

    poll_count++;
    while (fgets(buffer, sizeof(buffer), fh) != NULL)
    {
        int numfields = strsplit(buffer, fields, 32);

        /* need either 7 fields (partition) or at least 14 fields */
        if ((numfields != 7) && (numfields < 14))
            continue;

        char *disk_name = fields[2];

        for (ds = disklist, pre_ds = NULL; ds != NULL; // Corrected pre_ds initialization
             pre_ds = ds, ds = ds->next)
            if (strcmp(disk_name, ds->name) == 0)
                break;

        if (ds == NULL)
        { // If ds is NULL, pre_ds should point to the last element or be NULL if list is empty
            if (disklist == NULL)
            { // List was empty
                pre_ds = NULL;
            }
            else
            { // Find the actual pre_ds if ds was not found
                diskstats_t *temp_ds;
                for (temp_ds = disklist, pre_ds = NULL; temp_ds != NULL && strcmp(disk_name, temp_ds->name) != 0;
                     pre_ds = temp_ds, temp_ds = temp_ds->next)
                    ;
                // after this loop, if temp_ds is NULL, pre_ds is the last element.
            }
        }

        if (ds == NULL)
        {
            if ((ds = calloc(1, sizeof(*ds))) == NULL)
            {
                WARNING("disk plugin: calloc failed for diskstats_t.");
                continue;
            }

            if ((ds->name = strdup(disk_name)) == NULL)
            {
                free(ds);
                WARNING("disk plugin: strdup failed for disk_name.");
                continue;
            }

            if (pre_ds == NULL) // If list was empty or inserting at head
                disklist = ds;
            else
                pre_ds->next = ds;
        }

        is_disk = 0;
        if (numfields == 7)
        {
            /* Kernel 2.6, Partition */
            read_ops = atoll(fields[3]);
            read_sectors = atoll(fields[4]);
            write_ops = atoll(fields[5]);
            write_sectors = atoll(fields[6]);
        }
        else
        {
            assert(numfields >= 14);

            /* Kernel 4.18+, Discards */
            if (numfields >= 18)
            {
                discard_ops = atoll(fields[14]);
                discard_merged = atoll(fields[15]);
                discard_sectors = atoll(fields[16]);
                discard_time = atoll(fields[17]);
            }

            /* Kernel 5.5+, Flush */
            if (numfields >= 20)
            {
                flush_ops = atoll(fields[18]);
                flush_time = atoll(fields[19]);
            }

            read_ops = atoll(fields[3]);
            write_ops = atoll(fields[7]);

            read_sectors = atoll(fields[5]);
            write_sectors = atoll(fields[9]);

            is_disk = 1;
            read_merged = atoll(fields[4]);
            read_time = atoll(fields[6]);
            write_merged = atoll(fields[8]);
            write_time = atoll(fields[10]);

            in_progress = atof(fields[11]);

            io_time = atof(fields[12]);
            weighted_time = atof(fields[13]);
        }

        {
            derive_t diff_read_sectors;
            derive_t diff_write_sectors;

            /* If the counter wraps around, it's only 32 bits.. */
            if (read_sectors < ds->read_sectors)
                diff_read_sectors = 1 + read_sectors + (UINT_MAX - ds->read_sectors);
            else
                diff_read_sectors = read_sectors - ds->read_sectors;
            if (write_sectors < ds->write_sectors)
                diff_write_sectors = 1 + write_sectors + (UINT_MAX - ds->write_sectors);
            else
                diff_write_sectors = write_sectors - ds->write_sectors;

            ds->read_bytes += 512 * diff_read_sectors;
            ds->write_bytes += 512 * diff_write_sectors;
            ds->read_sectors = read_sectors;
            ds->write_sectors = write_sectors;
        }

        /* Calculate the average time an io-op needs to complete */
        if (is_disk)
        {
            derive_t diff_read_ops;
            derive_t diff_write_ops;
            derive_t diff_read_time;
            derive_t diff_write_time;

            if (read_ops < ds->read_ops)
                diff_read_ops = 1 + read_ops + (UINT_MAX - ds->read_ops);
            else
                diff_read_ops = read_ops - ds->read_ops;
            DEBUG("disk plugin: disk_name = %s; read_ops = %" PRIi64 "; "
                  "ds->read_ops = %" PRIi64 "; diff_read_ops = %" PRIi64 ";",
                  disk_name, read_ops, ds->read_ops, diff_read_ops);

            if (write_ops < ds->write_ops)
                diff_write_ops = 1 + write_ops + (UINT_MAX - ds->write_ops);
            else
                diff_write_ops = write_ops - ds->write_ops;

            if (read_time < ds->read_time)
                diff_read_time = 1 + read_time + (UINT_MAX - ds->read_time);
            else
                diff_read_time = read_time - ds->read_time;

            if (write_time < ds->write_time)
                diff_write_time = 1 + write_time + (UINT_MAX - ds->write_time);
            else
                diff_write_time = write_time - ds->write_time;

            if (io_time < ds->io_time)
                diff_io_time = 1 + io_time + (UINT_MAX - ds->io_time);
            else
                diff_io_time = io_time - ds->io_time;

            if (diff_read_ops != 0)
                ds->avg_read_time += disk_calc_time_incr(diff_read_time, diff_read_ops);
            if (diff_write_ops != 0)
                ds->avg_write_time +=
                    disk_calc_time_incr(diff_write_time, diff_write_ops);

            ds->read_ops = read_ops;
            ds->read_time = read_time;
            ds->write_ops = write_ops;
            ds->write_time = write_time;
            ds->io_time = io_time;

            if (read_merged || write_merged)
                ds->has_merged = true;

            if (!isnan(in_progress)) // Check if in_progress is a valid number
                ds->has_in_progress = true;

            if (io_time)
                ds->has_io_time = true;

            ds->has_discard =
                discard_ops || discard_merged || discard_sectors || discard_time;

            /* There is chance 'has_flush' is true while 'has_discard' remains false
             */
            ds->has_flush = flush_ops || flush_time;

        } /* if (is_disk) */

        /* Skip first cycle for newly-added disk */
        if (ds->poll_count == 0)
        {
            DEBUG("disk plugin: (ds->poll_count = 0) => Skipping.");
            ds->poll_count = poll_count; // Mark as processed in this cycle
            // Reset cumulative values for the first run to avoid huge initial spikes
            ds->read_bytes = 0;
            ds->write_bytes = 0;
            ds->avg_read_time = 0;
            ds->avg_write_time = 0;
            continue;
        }

        // Only submit if it's not the first cycle for this disk
        // And if there are actual operations to report to avoid spamming zeros
        // The check `(read_ops == 0) && (write_ops == 0)` was originally here,
        // but it refers to the current snapshot's ops, not the accumulated ds->read_ops etc.
        // Better to check accumulated values before submitting.
        if (ds->poll_count == poll_count && ds->poll_count > 1)
        { // Check if this is not the *absolute* first poll
            char *output_name = disk_name;

#if HAVE_LIBUDEV_H
            char *alt_name = NULL;
            if (conf_udev_name_attr != NULL && handle_udev != NULL)
            { // Ensure handle_udev is initialized
                alt_name =
                    disk_udev_attr_name(handle_udev, disk_name, conf_udev_name_attr);
                if (alt_name != NULL)
                    output_name = alt_name;
            }
#endif

            if (ignorelist_match(ignorelist, output_name) != 0)
            {
#if HAVE_LIBUDEV_H
                sfree(alt_name);
#endif
                continue;
            }

            if ((ds->read_bytes != 0) || (ds->write_bytes != 0))
                disk_submit(output_name, "disk_octets", ds->read_bytes, ds->write_bytes);

            // Submit raw operations count (current snapshot, not accumulated)
            disk_submit(output_name, "disk_ops", read_ops, write_ops);

            if ((ds->avg_read_time != 0) || (ds->avg_write_time != 0))
                disk_submit(output_name, "disk_time", ds->avg_read_time,
                            ds->avg_write_time);

            if (is_disk)
            {
                if (ds->has_merged)
                    disk_submit(output_name, "disk_merged", read_merged, write_merged);
                if (ds->has_in_progress && !isnan(in_progress))
                    submit_in_progress(output_name, in_progress);
                if (ds->has_io_time)
                    submit_io_time(output_name, io_time, weighted_time); // Submit current values
                if (report_discard && ds->has_discard)
                {
                    disk_submit_single(output_name, "disk_discard_ops", discard_ops);
                    disk_submit_single(output_name, "disk_discard_merged", discard_merged);
                    disk_submit_single(output_name, "disk_discard_sectors",
                                       discard_sectors);
                    disk_submit_single(output_name, "disk_discard_time", discard_time);
                }
                if (report_flush && ds->has_flush)
                {
                    disk_submit_single(output_name, "disk_flush_ops", flush_ops);
                    disk_submit_single(output_name, "disk_flush_time", flush_time);
                }
                submit_utilization(output_name, diff_io_time);
            } /* if (is_disk) */

#if HAVE_LIBUDEV_H
            sfree(alt_name);
#endif
            // Reset cumulative values for the next interval
            ds->read_bytes = 0;
            ds->write_bytes = 0;
            ds->avg_read_time = 0;
            ds->avg_write_time = 0;
        }
        ds->poll_count = poll_count; // Update poll count regardless of submission

    } /* while (fgets (buffer, sizeof (buffer), fh) != NULL) */

    /* Remove disks that have disappeared from diskstats */
    for (ds = disklist, pre_ds = NULL; ds != NULL;)
    {
        /* Disk exists */
        if (ds->poll_count == poll_count)
        {
            pre_ds = ds;
            ds = ds->next;
            continue;
        }

        /* Disk is missing, remove it */
        diskstats_t *missing_ds = ds;
        if (ds == disklist)
        { // If removing the head
            disklist = ds->next;
            // pre_ds remains NULL or becomes disklist if needed in next iteration
        }
        else if (pre_ds != NULL)
        { // Should always be true if not head unless list was only 1 element
            pre_ds->next = ds->next;
        }
        // If pre_ds is NULL and it's not the head, something is wrong or list had 1 element and was removed
        // This part of logic might need care if list had only one element which is now removed.
        // `pre_ds` would be NULL, and `disklist` would be NULL too.

        ds = ds->next; // Move to next before freeing

        DEBUG("disk plugin: Disk %s disappeared.", missing_ds->name);
        free(missing_ds->name);
        free(missing_ds);
    }
    if (fh != NULL)
        fclose(fh); // Ensure fh is not NULL before closing
    return 0;
} /* int disk_read */

void module_register(void)
{
    plugin_register_config("disk", disk_config, config_keys, config_keys_num);
    plugin_register_init("disk", disk_init);
    plugin_register_shutdown("disk", disk_shutdown);
    plugin_register_read("disk", disk_read);
} /* void module_register */
