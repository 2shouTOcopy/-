#pragma once

#include <string>
#include <vector>
#include <limits> // For std::numeric_limits

// 确保这些定义是可访问的，或者在此处重新定义
#ifndef DATA_MAX_NAME_LEN
#define DATA_MAX_NAME_LEN 64 // 或者从你的 CommonDef.h 等头文件引入
#endif

// 数据源类型枚举 (与 collectd 内部一致)
enum dstype_t {
  DS_TYPE_COUNTER = 0,
  DS_TYPE_GAUGE = 1,
  DS_TYPE_DERIVE = 2,
  DS_TYPE_ABSOLUTE = 3,
  DS_TYPE_UNDEFINED = -1 // 自定义，用于错误处理
};

struct data_source_s {
  char name[DATA_MAX_NAME_LEN];
  int type; // dstype_t
  double min;
  double max;
};
typedef struct data_source_s data_source_t;

struct data_set_s {
  char type[DATA_MAX_NAME_LEN]; // Dataset name, e.g., "cpu", "memory"
  size_t ds_num;
  data_source_t *ds; // 动态分配的数组
};
typedef struct data_set_s data_set_t;

// 全局存储解析后的 types.db 数据
// 或者由 ConfigManager 持有，并通过参数传递
// extern std::vector<data_set_t> g_parsed_datasets;

namespace TypesDbParser {

/**
 * @brief Parses a types.db file into a vector of data_set_t structures.
 *
 * @param filename The path to the types.db file.
 * @param out_datasets Vector to store the parsed datasets.
 * The caller is responsible for freeing the 'ds' member of each data_set_t
 * if the function succeeds.
 * @return 0 on success, -1 on failure.
 */
int parse_file(const char* filename, std::vector<data_set_t>& out_datasets);

/**
 * @brief Frees the dynamically allocated memory within a vector of data_set_t.
 *
 * @param datasets The vector of datasets to clean up.
 */
void free_datasets(std::vector<data_set_t>& datasets);

} // namespace TypesDbParser
