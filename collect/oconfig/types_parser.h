#pragma once

#include <string>
#include <vector>
#include <limits> 

#include "ModuleDef.h"

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

}
