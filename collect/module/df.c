/**
 * collectd - src/df.c
 * Copyright (C) 2005-2009  Florian octo Forster
 * Copyright (C) 2009       Paul Sadauskas
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
 * Paul Sadauskas <psadauskas at gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils/mount/mount.h" // Relies on Linux-specific implementation in mount.c

// Assume Linux environment, statvfs is available and preferred.
#include <sys/statvfs.h>
#include <string.h> // For strcmp, strncmp, strlen
#include <stdint.h> // For uint64_t, int64_t

#define STATANYFS statvfs
#define STATANYFS_STR "statvfs"
// For statvfs, f_frsize is the fundamental filesystem block size.
// f_bsize is the preferred I/O block size, may differ.
// Standard practice is to use f_frsize for space calculations.
#define BLOCKSIZE(s) ((s).f_frsize)

static const char *config_keys[] = {
    "Device", "MountPoint", "FSType",
    "IgnoreSelected", "ReportByDevice", "ReportInodes",
    "ValuesAbsolute", "ValuesPercentage", "LogOnce"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *il_device;
static ignorelist_t *il_mountpoint;
static ignorelist_t *il_fstype;
static ignorelist_t *il_errors;

static bool by_device;
static bool report_inodes;
static bool values_absolute = true;
static bool values_percentage;
static bool log_once;

static int df_init(void)
{
    // Create ignorelists if they don't exist.
    // This function might be called multiple times if reconfiguring,
    // so check for NULL.
    if (il_device == NULL)
        il_device = ignorelist_create(/* invert = */ true);
    if (il_mountpoint == NULL)
        il_mountpoint = ignorelist_create(/* invert = */ true);
    if (il_fstype == NULL)
        il_fstype = ignorelist_create(/* invert = */ true);
    if (il_errors == NULL)
        il_errors = ignorelist_create(/* invert = */ true);

    return 0;
}

static int df_config(const char *key, const char *value)
{
    // df_init() is called by plugin_register_config before this,
    // or should be called if this can be invoked independently after init.
    // For safety, ensure lists are created if called standalone.
    if (il_device == NULL)
        df_init();

    if (strcasecmp(key, "Device") == 0)
    {
        if (ignorelist_add(il_device, value))
            return 1;
        return 0;
    }
    else if (strcasecmp(key, "MountPoint") == 0)
    {
        if (ignorelist_add(il_mountpoint, value))
            return 1;
        return 0;
    }
    else if (strcasecmp(key, "FSType") == 0)
    {
        if (ignorelist_add(il_fstype, value))
            return 1;
        return 0;
    }
    else if (strcasecmp(key, "IgnoreSelected") == 0)
    {
        bool invert_based_on_value = !IS_TRUE(value); // True = "ignore listed", so normal ignorelist behavior (invert=0)
                                                      // False = "select listed", so inverted ignorelist behavior (invert=1)
        ignorelist_set_invert(il_device, invert_based_on_value);
        ignorelist_set_invert(il_mountpoint, invert_based_on_value);
        ignorelist_set_invert(il_fstype, invert_based_on_value);
        return 0;
    }
    else if (strcasecmp(key, "ReportByDevice") == 0)
    {
        by_device = IS_TRUE(value);
        return 0;
    }
    else if (strcasecmp(key, "ReportInodes") == 0)
    {
        report_inodes = IS_TRUE(value);
        return 0;
    }
    else if (strcasecmp(key, "ValuesAbsolute") == 0)
    {
        values_absolute = IS_TRUE(value);
        return 0;
    }
    else if (strcasecmp(key, "ValuesPercentage") == 0)
    {
        values_percentage = IS_TRUE(value);
        return 0;
    }
    else if (strcasecmp(key, "LogOnce") == 0)
    {
        log_once = IS_TRUE(value);
        return 0;
    }

    return -1;
}

__attribute__((nonnull(2))) static void df_submit_one(char *plugin_instance,
                                                      const char *type,
                                                      const char *type_instance,
                                                      gauge_t value)
{
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &(value_t){.gauge = value};
    vl.values_len = 1;
    sstrncpy(vl.plugin, "df", sizeof(vl.plugin));
    if (plugin_instance != NULL)
        sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, type, sizeof(vl.type));
    if (type_instance != NULL)
        sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

    plugin_dispatch_values(&vl);
} /* void df_submit_one */

static int df_read(void)
{
    struct statvfs statbuf; // Using statvfs directly for Linux
    int retval = 0;
    cu_mount_t *mnt_list;

    mnt_list = NULL;
    // cu_mount_getlist will use the Linux-specific implementation
    // from utils/mount/mount.c to read /proc/mounts or similar.
    if (cu_mount_getlist(&mnt_list) == NULL)
    {
        ERROR("df plugin: cu_mount_getlist failed.");
        return -1;
    }

    for (cu_mount_t *mnt_ptr = mnt_list; mnt_ptr != NULL;
         mnt_ptr = mnt_ptr->next)
    {
        unsigned long long blocksize;
        char disk_name[DATA_MAX_NAME_LEN]; // Use DATA_MAX_NAME_LEN
        cu_mount_t *dup_ptr;
        uint64_t blk_free;
        uint64_t blk_reserved;
        uint64_t blk_used;

        char const *dev =
            (mnt_ptr->spec_device != NULL) ? mnt_ptr->spec_device : mnt_ptr->device;

        if (ignorelist_match(il_device, dev))
            continue;
        if (ignorelist_match(il_mountpoint, mnt_ptr->dir))
            continue;
        if (ignorelist_match(il_fstype, mnt_ptr->type))
            continue;

        /* search for duplicates *in front of* the current mnt_ptr. */
        for (dup_ptr = mnt_list; dup_ptr != NULL; dup_ptr = dup_ptr->next)
        {
            if (dup_ptr == mnt_ptr)
            { /* No duplicate found: mnt_ptr is the first of its kind. */
                dup_ptr = NULL;
                break;
            }
            /* Duplicate found if reporting by device and devices match,
             * OR if reporting by mountpoint (default) and mountpoints match. */
            if (by_device)
            {
                if ((mnt_ptr->spec_device != NULL) && (dup_ptr->spec_device != NULL) &&
                    (strcmp(mnt_ptr->spec_device, dup_ptr->spec_device) == 0))
                    break;
            }
            else
            {
                if (strcmp(mnt_ptr->dir, dup_ptr->dir) == 0)
                    break;
            }
        }

        if (dup_ptr != NULL) /* ignore duplicates */
            continue;

        if (STATANYFS(mnt_ptr->dir, &statbuf) < 0)
        { // STATANYFS is now always statvfs
            if (log_once == false || ignorelist_match(il_errors, mnt_ptr->dir) == 0)
            {
                if (log_once == true)
                {
                    ignorelist_add(il_errors, mnt_ptr->dir);
                }
                ERROR(STATANYFS_STR "(\"%s\") failed: %s", mnt_ptr->dir, STRERRNO); // STATANYFS_STR is "statvfs"
            }
            continue;
        }
        else
        {
            if (log_once == true)
            {
                ignorelist_remove(il_errors, mnt_ptr->dir);
            }
        }

        if (statbuf.f_blocks == 0) // f_blocks is total data blocks in file system. Skip if zero.
            continue;

        if (by_device)
        {
            if (strncmp(dev, "/dev/", strlen("/dev/")) == 0)
                sstrncpy(disk_name, dev + strlen("/dev/"), sizeof(disk_name));
            else
                sstrncpy(disk_name, dev, sizeof(disk_name));

            if (strlen(disk_name) < 1)
            {
                DEBUG("df: no device name for mountpoint %s, skipping", mnt_ptr->dir);
                continue;
            }
        }
        else
        {
            if (strcmp(mnt_ptr->dir, "/") == 0)
                sstrncpy(disk_name, "root", sizeof(disk_name));
            else
            {
                sstrncpy(disk_name, mnt_ptr->dir + 1, sizeof(disk_name)); // Skip leading '/'
                size_t len = strlen(disk_name);
                for (size_t i = 0; i < len; i++)
                    if (disk_name[i] == '/')
                        disk_name[i] = '-'; // Replace subsequent '/' with '-'
            }
        }

        blocksize = BLOCKSIZE(statbuf); // BLOCKSIZE now refers to statvfs version (f_frsize)
        if (blocksize == 0)
        { // Avoid division by zero if f_frsize is faulty
            ERROR("df plugin: Filesystem %s reported blocksize of 0. Skipping.", disk_name);
            continue;
        }

        /* Sanity checks for block counts from statvfs */
        if (statbuf.f_bavail > statbuf.f_bfree)
        { // Available cannot be more than free
            DEBUG("df plugin: f_bavail (%llu) > f_bfree (%llu) for %s. Adjusting f_bavail.",
                  (unsigned long long)statbuf.f_bavail, (unsigned long long)statbuf.f_bfree, disk_name);
            statbuf.f_bavail = statbuf.f_bfree;
        }
        if (statbuf.f_bfree > statbuf.f_blocks)
        { // Free cannot be more than total
            DEBUG("df plugin: f_bfree (%llu) > f_blocks (%llu) for %s. Adjusting f_bfree.",
                  (unsigned long long)statbuf.f_bfree, (unsigned long long)statbuf.f_blocks, disk_name);
            statbuf.f_bfree = statbuf.f_blocks;
        }
        // Note: POSIX f_bavail can be negative in some FS (if space is overcommitted for root),
        // but it's unsigned. The original code cast to int64_t. Let's use it directly as uint64_t.
        // If f_bavail is very large (wrapping around from negative), it might be an issue.
        // However, standard interpretation is that f_bavail is for non-privileged users.
        // df utility on Linux shows 'Available' based on f_bavail.

        blk_free = (uint64_t)statbuf.f_bavail;                         // Free blocks available to non-superuser
        blk_reserved = (uint64_t)(statbuf.f_bfree - statbuf.f_bavail); // Reserved blocks (superuser free - non-superuser free)
        blk_used = (uint64_t)(statbuf.f_blocks - statbuf.f_bfree);     // Used blocks (total - superuser free)

        if (values_absolute)
        {
            df_submit_one(disk_name, "df_complex", "free",
                          (gauge_t)((long double)blk_free * blocksize));
            df_submit_one(disk_name, "df_complex", "reserved",
                          (gauge_t)((long double)blk_reserved * blocksize));
            df_submit_one(disk_name, "df_complex", "used",
                          (gauge_t)((long double)blk_used * blocksize));
        }

        if (values_percentage)
        {
            uint64_t total_for_percentage = statbuf.f_blocks; // Denominator for percentage is total blocks
            if (total_for_percentage > 0)
            {
                df_submit_one(disk_name, "percent_bytes", "free",
                              (gauge_t)(((long double)blk_free / total_for_percentage) * 100.0L));
                df_submit_one(disk_name, "percent_bytes", "reserved",
                              (gauge_t)(((long double)blk_reserved / total_for_percentage) * 100.0L));
                df_submit_one(disk_name, "percent_bytes", "used",
                              (gauge_t)(((long double)blk_used / total_for_percentage) * 100.0L));
            }
            else
            {
                WARNING("df plugin: f_blocks is zero for %s, cannot report byte percentage.", disk_name);
            }
        }

        /* Inode handling */
        if (report_inodes && statbuf.f_files != 0)
        {
            uint64_t inode_free;
            uint64_t inode_reserved;
            uint64_t inode_used;

            /* Sanity checks for inode counts */
            if (statbuf.f_favail > statbuf.f_ffree)
            { // Available cannot be more than free
                DEBUG("df plugin: f_favail (%llu) > f_ffree (%llu) for %s. Adjusting f_favail.",
                      (unsigned long long)statbuf.f_favail, (unsigned long long)statbuf.f_ffree, disk_name);
                statbuf.f_favail = statbuf.f_ffree;
            }
            if (statbuf.f_ffree > statbuf.f_files)
            { // Free cannot be more than total
                DEBUG("df plugin: f_ffree (%llu) > f_files (%llu) for %s. Adjusting f_ffree.",
                      (unsigned long long)statbuf.f_ffree, (unsigned long long)statbuf.f_files, disk_name);
                statbuf.f_ffree = statbuf.f_files;
            }

            inode_free = (uint64_t)statbuf.f_favail;                         // Free inodes available to non-superuser
            inode_reserved = (uint64_t)(statbuf.f_ffree - statbuf.f_favail); // Reserved inodes
            inode_used = (uint64_t)(statbuf.f_files - statbuf.f_ffree);      // Used inodes

            if (values_absolute)
            {
                df_submit_one(disk_name, "df_inodes", "free", (gauge_t)inode_free);
                df_submit_one(disk_name, "df_inodes", "reserved", (gauge_t)inode_reserved);
                df_submit_one(disk_name, "df_inodes", "used", (gauge_t)inode_used);
            }
            if (values_percentage)
            {
                uint64_t total_inodes_for_percentage = statbuf.f_files;
                if (total_inodes_for_percentage > 0)
                {
                    df_submit_one(disk_name, "percent_inodes", "free",
                                  (gauge_t)(((long double)inode_free / total_inodes_for_percentage) * 100.0L));
                    df_submit_one(disk_name, "percent_inodes", "reserved",
                                  (gauge_t)(((long double)inode_reserved / total_inodes_for_percentage) * 100.0L));
                    df_submit_one(disk_name, "percent_inodes", "used",
                                  (gauge_t)(((long double)inode_used / total_inodes_for_percentage) * 100.0L));
                }
                else
                {
                    WARNING("df plugin: f_files is zero for %s, cannot report inode percentage.", disk_name);
                }
            }
        }
    } /* end for loop iterating mnt_list */

    cu_mount_freelist(mnt_list);
    return retval;
} /* int df_read */

static int df_shutdown(void)
{
    ignorelist_destroy(il_device);
    il_device = NULL;
    ignorelist_destroy(il_mountpoint);
    il_mountpoint = NULL;
    ignorelist_destroy(il_fstype);
    il_fstype = NULL;
    ignorelist_destroy(il_errors);
    il_errors = NULL;
    return 0;
}

void module_register(void)
{
    plugin_register_config("df", df_config, config_keys, config_keys_num);
    plugin_register_init("df", df_init);
    plugin_register_read("df", df_read);
    plugin_register_shutdown("df", df_shutdown); // Register shutdown function
} /* void module_register */
