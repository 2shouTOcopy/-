#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <memory>
#include <string.h>

#include "csv.h"
#include "../daemon/PluginService.h"
#include "../daemon/utils/utils.h"

/* ───────────────────────────────────────────
 * 内部工具
 * ─────────────────────────────────────────── */

namespace
{

    inline void rstripSlash(std::string &s)
    {
        while (!s.empty() && s.back() == '/')
            s.pop_back();
    }

} // namespace

int CCsvModule::config(const std::string &key, const std::string &val)
{
    if (key == "DataDir")
    {
        _useStdout = _useStderr = false;
        _dataDir = {};

        if (val == "stdout")
            _useStdout = true;
        else if (val == "stderr")
            _useStderr = true;
        else
        {
            _dataDir = val;
            rstripSlash(_dataDir);
        }
    }
    else if (key == "StoreRates")
    {
        _storeRates = IS_TRUE(val.c_str());
    }
    else if (key == "FileDate")
    {
        _withDate = IS_TRUE(val.c_str());
    }
    else
	{
		return -1;
	}

	ERROR(" >>>>>>>>>>>>> [%s -> %s]", key.c_str(), val.c_str());
    return 0;
}

/* 把 value_list 转成一行 CSV 文本 */
int CCsvModule::vlToString(std::string &out,
                          const data_set_t *ds,
                          const value_list_t *vl) const
{
    assert(ds && vl && ds->ds_num == vl->values_len);
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << CDTIME_T_TO_DOUBLE(vl->time);

    std::unique_ptr<gauge_t[]> rates;
    for (size_t i = 0; i < ds->ds_num; ++i)
    {
        const auto &dsrc = ds->ds[i];
        const auto &val = vl->values[i];

        if (dsrc.type == DS_TYPE_GAUGE)
        {
            oss << ',' << val.gauge;
        }
        else if (_storeRates)
        {
			#if 0
            if (!rates)
                rates.reset(uc_get_rate(ds, vl));
            if (!rates)
                return -1;
            oss << ',' << rates[i];
			#endif
        }
        else if (dsrc.type == DS_TYPE_COUNTER)
        {
            oss << ',' << static_cast<uint64_t>(val.counter);
        }
        else if (dsrc.type == DS_TYPE_DERIVE)
        {
            oss << ',' << val.derive;
        }
        else if (dsrc.type == DS_TYPE_ABSOLUTE)
        {
            oss << ',' << static_cast<uint64_t>(val.absolute);
        }
    }
    out = oss.str();
    return 0;
}

/* 生成（可能带日期）的 CSV 路径 */
int CCsvModule::vlToPath(std::string &path,
                        const value_list_t *vl) const
{
    char buf[512]{};

    /* 前缀目录 */
    std::string pfx;
    if (!_dataDir.empty())
    {
        pfx = _dataDir + '/';
    }

    /* FORMAT_VL → path body */
    if (FORMAT_VL(buf, sizeof(buf), vl) != 0)
        return -1;

    path = pfx + buf;

    if ((_useStdout || _useStderr) || !_withDate)
        return 0; // 不加日期

    /* 追加 -YYYY-MM-DD */
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    if (!localtime_r(&now, &tmv))
        return -1;

    char datebuf[16];
    if (std::strftime(datebuf, sizeof(datebuf), "-%Y-%m-%d", &tmv) == 0)
        return -1;

    path += datebuf;
    return 0;
}

/* 若文件不存在则创建并写表头 */
bool CCsvModule::touchCsv(const std::string &file,
                         const data_set_t *ds) const
{
    struct stat st{};
    if (stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        return true; // 已存在

    if (check_create_dir(file.c_str()) != 0)
        return false;

    FILE *fp = std::fopen(file.c_str(), "w");
    if (!fp)
    {
        ERROR("csv: fopen(%s) failed: %s", file.c_str(), strerror(errno));
        return false;
    }
    std::fprintf(fp, "epoch");
    for (size_t i = 0; i < ds->ds_num; ++i)
        std::fprintf(fp, ",%s", ds->ds[i].name);
    std::fprintf(fp, "\n");
    std::fclose(fp);
    return true;
}

/* 真正的写回调 */
int CCsvModule::write(const data_set_t *ds, const value_list_t *vl)
{
	INFO("CCsvModule write");
    if (!ds || !vl || 0 != strcmp(ds->type, vl->type))
    {
		ERROR("CCsvModule write %s %s", ds->type, vl->type);
        return -1;
	}

    /* 1) 计算内容行 */
    std::string line;
    if (vlToString(line, ds, vl) != 0)
    {
		ERROR("CCsvModule write 2");
        return -1;
	}

    /* stdout/stderr 模式 */
    if (_useStdout || _useStderr)
    {
        /* 先把文件名转义后拼成 PUTVAL 行 */
        char id[512];
        if (FORMAT_VL(id, sizeof(id), vl) != 0)
            return -1;
        escape_string(id, sizeof(id));
        for (char &c : line)
            if (c == ',')
                c = ':'; // PUTVAL 使用冒号

        std::ostream &os =
            _useStdout ? std::cout : std::cerr;
        os << "PUTVAL " << id
           << " interval=" << CDTIME_T_TO_DOUBLE(vl->interval)
           << ' ' << line << '\n';
        return 0;
    }

    /* 2) 生成文件路径 */
    std::string file;
    if (vlToPath(file, vl) != 0)
        return -1;

    /* 3) 若首次则创建并写表头 */
    if (!touchCsv(file, ds))
        return -1;

    /* 4) 追加数据行（带进程间锁） */
    std::lock_guard<std::mutex> lg(_ioMtx);
    FILE *fp = std::fopen(file.c_str(), "a");
    if (!fp)
    {
        ERROR("csv: fopen(%s) failed: %s", file.c_str(), strerror(errno));
        return -1;
    }

    int fd = fileno(fp);
    struct flock lk
    {
    };
    lk.l_type = F_WRLCK;
    lk.l_whence = SEEK_SET;
    lk.l_pid = getpid();
    if (fcntl(fd, F_SETLK, &lk) != 0)
    {
        ERROR("csv: flock(%s) failed: %s", file.c_str(), strerror(errno));
        std::fclose(fp);
        return -1;
    }

    std::fprintf(fp, "%s\n", line.c_str());
    std::fclose(fp); // 自动释放锁
    return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CCsvModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}

