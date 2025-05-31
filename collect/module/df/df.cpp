#include <sys/statvfs.h>
#include <cstring>
#include <cerrno>
#include <assert.h>

#include "df.h"
#include "../daemon/PluginService.h"
#include "../daemon/utils/utils.h"
#include "../daemon/utils/mount.h"

int CDfModule::init()
{
	return 0;
}

int CDfModule::config(const std::string &key, const std::string &val)
{
    if (key == "Device")
    {
        m_ilDevice.add(val);
    }
    else if (key == "MountPoint")
    {
        m_ilMountPoint.add(val);
    }
    else if (key == "FSType")
    {
        m_ilFsType.add(val);
    }
    else if (key == "IgnoreSelected")
    {
        bool ignoreListed = IS_TRUE(val.c_str());
        bool inv = !ignoreListed;
        m_ilDevice.setInvert(inv);
        m_ilMountPoint.setInvert(inv);
        m_ilFsType.setInvert(inv);
    }
    else if (key == "ReportByDevice")
    {
        m_bDevice = IS_TRUE(val.c_str());
    }
    else if (key == "ReportInodes")
    {
        m_bReportInodes = IS_TRUE(val.c_str());
    }
    else if (key == "ValuesAbsolute")
    {
        m_bAbsolute = IS_TRUE(val.c_str());
    }
    else if (key == "ValuesPercentage")
    {
        m_bPercentage = IS_TRUE(val.c_str());
    }
    else if (key == "LogOnce")
    {
        m_bLogOnce = IS_TRUE(val.c_str());
    }
    else
    {
        return -1;
    }
    return 0;
}

void CDfModule::submitValue(const std::string &pluginInstance,
                            const std::string &type,
                            const std::string &typeInstance,
                            gauge_t value)
{
    value_list_t vl = VALUE_LIST_INIT;
    value_t tmp = {.gauge = value};

    vl.values = &tmp;
    vl.values_len = 1;

    sstrncpy(vl.plugin, "df", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, pluginInstance.c_str(), sizeof(vl.plugin_instance));
    sstrncpy(vl.type, type.c_str(), sizeof(vl.type));
    sstrncpy(vl.type_instance, typeInstance.c_str(), sizeof(vl.type_instance));

	INFO("Submitting metric: plugin='%s', type='%s', type_instance='%s', value=%lf",
	     vl.plugin, vl.type, vl.type_instance, value);

    PluginService::Instance().dispatchValues(&vl);
}

int CDfModule::read()
{
    struct statvfs st{ };
    cu_mount_t *mnt_list = nullptr;
    if (cu_mount_getlist(&mnt_list) == nullptr)
    {
        ERROR("df plugin: cu_mount_getlist failed.");
        return -1;
    }

    for (auto *mnt = mnt_list; mnt; mnt = mnt->next)
    {
		INFO("dir %s, spec_device %s, device %s, type %s, options %s", 
			mnt->dir, mnt->spec_device, mnt->device, mnt->type, mnt->options);
        const char *dev = mnt->spec_device ? mnt->spec_device : mnt->device;
        std::string dev_s(dev), dir_s(mnt->dir), type_s(mnt->type);

        // 忽略规则
        if (m_ilDevice.match(dev_s) 
			|| m_ilMountPoint.match(dir_s)
			|| m_ilFsType.match(type_s))
        {
			continue;
		}

        // 去重：先前同设备或同挂载点已处理过
        bool is_dup = false;
        for (auto *dup = mnt_list; dup != mnt; dup = dup->next)
        {
            if (m_bDevice)
            {
                if (dup->spec_device && mnt->spec_device &&
                    strcmp(dup->spec_device, mnt->spec_device) == 0)
                {
                    is_dup = true;
                    break;
                }
            }
            else
            {
                if (strcmp(dup->dir, mnt->dir) == 0)
                {
                    is_dup = true;
                    break;
                }
            }
        }
        if (is_dup)
            continue;

        // statvfs
        if (statvfs(mnt->dir, &st) < 0)
        {
            if (!m_bLogOnce || !m_ilErrors.match(dir_s))
            {
                if (m_bLogOnce)
                    m_ilErrors.add(dir_s);
                ERROR("statvfs(\"%s\") failed: %s", mnt->dir, strerror(errno));
            }
            continue;
        }
        else if (m_bLogOnce)
        {
            m_ilErrors.remove(dir_s);
        }

        if (st.f_blocks == 0)
            continue;

        // 构造实例名
        std::string inst;
        if (m_bDevice)
        {
            const char *p = (strncmp(dev, "/dev/", 5) == 0) ? dev + 5 : dev;
            inst = (std::strlen(p) ? p : "unknown");
        }
        else
        {
            if (strcmp(mnt->dir, "/") == 0)
            {
                inst = "root";
            }
            else
            {
                inst = mnt->dir + 1;
                for (auto &c : inst)
                    if (c == '/')
                        c = '-';
            }
        }

        uint64_t blk_size = st.f_frsize;
        uint64_t free_blk = st.f_bavail;
        uint64_t res_blk = st.f_bfree - st.f_bavail;
        uint64_t used_blk = st.f_blocks - st.f_bfree;

        // 绝对值上报
        if (m_bAbsolute)
        {
            submitValue(inst, "df_complex", "free", free_blk * blk_size);
            submitValue(inst, "df_complex", "reserved", res_blk * blk_size);
            submitValue(inst, "df_complex", "used", used_blk * blk_size);
        }
        // 百分比上报
        if (m_bPercentage && st.f_blocks > 0)
        {
            long double total = st.f_blocks;
            submitValue(inst, "percent_bytes", "free", (gauge_t)(free_blk / total * 100.0L));
            submitValue(inst, "percent_bytes", "reserved", (gauge_t)(res_blk / total * 100.0L));
            submitValue(inst, "percent_bytes", "used", (gauge_t)(used_blk / total * 100.0L));
        }

        // Inodes
        if (m_bReportInodes && st.f_files > 0)
        {
            uint64_t ifree = st.f_favail;
            uint64_t ires = st.f_ffree - st.f_favail;
            uint64_t iused = st.f_files - st.f_ffree;

            if (m_bAbsolute)
            {
                submitValue(inst, "df_inodes", "free", ifree);
                submitValue(inst, "df_inodes", "reserved", ires);
                submitValue(inst, "df_inodes", "used", iused);
            }
            if (m_bPercentage)
            {
                long double itot = st.f_files;
                submitValue(inst, "percent_inodes", "free", (gauge_t)(ifree / itot * 100.0L));
                submitValue(inst, "percent_inodes", "reserved", (gauge_t)(ires / itot * 100.0L));
                submitValue(inst, "percent_inodes", "used", (gauge_t)(iused / itot * 100.0L));
            }
        }
    }

    cu_mount_freelist(mnt_list);
    return 0;
}

int CDfModule::shutdown()
{
	m_ilDevice.clear();
	m_ilMountPoint.clear();
	m_ilFsType.clear();
	m_ilErrors.clear();
	return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CDfModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}

