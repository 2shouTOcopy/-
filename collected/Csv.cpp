// CSVPlugin.cpp
#include "CSVPlugin.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <sstream>

/* ───────────────────────────────────────────
 * 内部工具
 * ─────────────────────────────────────────── */

namespace {

inline void rstripSlash(std::string& s)
{
    while (!s.empty() && s.back() == '/')
        s.pop_back();
}

} // namespace

/* ───────────────────────────────────────────
 * CAbstractUserModule 实现
 * ─────────────────────────────────────────── */

int CSVPlugin::config(const std::string& key, const std::string& val)
{
    if (strcasecmp(key.c_str(), "DataDir") == 0) {
        _useStdout = _useStderr = false;
        _dataDir   = {};

        if (strcasecmp(val.c_str(), "stdout") == 0)
            _useStdout = true;
        else if (strcasecmp(val.c_str(), "stderr") == 0)
            _useStderr = true;
        else {
            _dataDir = val;
            rstripSlash(_dataDir);
        }
        return 0;
    }
    if (strcasecmp(key.c_str(), "StoreRates") == 0) {
        _storeRates = IS_TRUE(val.c_str());
        return 0;
    }
    if (strcasecmp(key.c_str(), "FileDate") == 0) {
        _withDate = IS_TRUE(val.c_str());
        return 0;
    }
    return -1;               // 未识别
}

/* 把 value_list 转成一行 CSV 文本 */
int CSVPlugin::vlToString(std::string& out,
                          const data_set_t* ds,
                          const value_list_t* vl) const
{
    assert(ds && vl && ds->ds_num == vl->values_len);
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << CDTIME_T_TO_DOUBLE(vl->time);

    std::unique_ptr<gauge_t[]> rates;
    for (size_t i = 0; i < ds->ds_num; ++i) {
        const auto& dsrc = ds->ds[i];
        const auto& val  = vl->values[i];

        if (dsrc.type == DS_TYPE_GAUGE) {
            oss << ',' << val.gauge;
        } else if (_storeRates) {
            if (!rates) rates.reset(uc_get_rate(ds, vl));
            if (!rates) return -1;
            oss << ',' << rates[i];
        } else if (dsrc.type == DS_TYPE_COUNTER) {
            oss << ',' << static_cast<uint64_t>(val.counter);
        } else if (dsrc.type == DS_TYPE_DERIVE) {
            oss << ',' << val.derive;
        } else if (dsrc.type == DS_TYPE_ABSOLUTE) {
            oss << ',' << static_cast<uint64_t>(val.absolute);
        }
    }
    out = oss.str();
    return 0;
}

/* 生成（可能带日期）的 CSV 路径 */
int CSVPlugin::vlToPath(std::string& path,
                        const value_list_t* vl) const
{
    char buf[512]{};

    /* 前缀目录 */
    std::string pfx;
    if (!_dataDir.empty()) {
        pfx = _dataDir + '/';
    }

    /* FORMAT_VL → path body */
    if (FORMAT_VL(buf, sizeof(buf), vl) != 0)
        return -1;

    path = pfx + buf;

    if ((_useStdout || _useStderr) || !_withDate)
        return 0;               // 不加日期

    /* 追加 -YYYY-MM-DD */
    std::time_t now   = std::time(nullptr);
    std::tm      tmv{};
    if (!localtime_r(&now, &tmv))
        return -1;

    char datebuf[16];
    if (std::strftime(datebuf, sizeof(datebuf), "-%Y-%m-%d", &tmv) == 0)
        return -1;

    path += datebuf;
    return 0;
}

/* 若文件不存在则创建并写表头 */
bool CSVPlugin::touchCsv(const std::string& file,
                         const data_set_t* ds) const
{
    struct stat st{};
    if (stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        return true;            // 已存在

    if (check_create_dir(file.c_str()) != 0)
        return false;

    FILE* fp = std::fopen(file.c_str(), "w");
    if (!fp) {
        ERROR("csv: fopen(%s) failed: %s", file.c_str(), STRERRNO);
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
int CSVPlugin::write(const data_set_t* ds,
                     const value_list_t* vl)
{
    if (!ds || !vl || 0 != std::strcmp(ds->type, vl->type))
        return -1;

    /* 1) 计算内容行 */
    std::string line;
    if (vlToString(line, ds, vl) != 0)
        return -1;

    /* stdout/stderr 模式 */
    if (_useStdout || _useStderr) {
        /* 先把文件名转义后拼成 PUTVAL 行 */
        char id[512];
        if (FORMAT_VL(id, sizeof(id), vl) != 0)
            return -1;
        escape_string(id, sizeof(id));
        for (char& c : line)
            if (c == ',') c = ':';      // PUTVAL 使用冒号

        std::ostream& os =
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
    FILE* fp = std::fopen(file.c_str(), "a");
    if (!fp) {
        ERROR("csv: fopen(%s) failed: %s", file.c_str(), STRERRNO);
        return -1;
    }

    int fd = fileno(fp);
    struct flock lk{};
    lk.l_type   = F_WRLCK;
    lk.l_whence = SEEK_SET;
    lk.l_pid    = getpid();
    if (fcntl(fd, F_SETLK, &lk) != 0) {
        ERROR("csv: flock(%s) failed: %s", file.c_str(), STRERRNO);
        std::fclose(fp);
        return -1;
    }

    std::fprintf(fp, "%s\n", line.c_str());
    std::fclose(fp);            // 自动释放锁
    return 0;
}