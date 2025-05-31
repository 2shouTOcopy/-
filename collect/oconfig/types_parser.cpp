#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "types_parser.h"

namespace TypesDbParser {

// Helper function to trim whitespace from start and end of string
static std::string trim_string(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(start, end - start + 1);
}

// Helper function to parse data source type string
static int parse_ds_type(const std::string& type_str) {
    if (type_str == "GAUGE") return DS_TYPE_GAUGE;
    if (type_str == "COUNTER") return DS_TYPE_COUNTER;
    if (type_str == "DERIVE") return DS_TYPE_DERIVE;
    if (type_str == "ABSOLUTE") return DS_TYPE_ABSOLUTE;
    std::cerr << "TypesDbParser: Unknown data source type: " << type_str << std::endl;
    return DS_TYPE_UNDEFINED;
}

// Helper function to parse min/max value string
static double parse_ds_value(const std::string& value_str, bool is_min) {
    if (value_str == "U") {
        return is_min ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    try {
        return std::stod(value_str);
    } catch (const std::exception& e) {
        std::cerr << "TypesDbParser: Cannot parse value '" << value_str << "': " << e.what() << std::endl;
        return is_min ? 0.0 : std::numeric_limits<double>::quiet_NaN(); // Or some other error indicator
    }
}

// Helper to split string by delimiter
static std::vector<std::string> split_string(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(trim_string(token));
    }
    return tokens;
}


int parse_file(const char* filename, std::vector<data_set_t>& out_datasets) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        std::cerr << "TypesDbParser: Cannot open types.db file: " << filename << std::endl;
        return -1;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(infile, line)) {
        line_num++;
        line = trim_string(line);

        if (line.empty() || line[0] == '#') {
            continue; // Skip empty lines and comments
        }

        // Split line into type_name and ds_specs_str
        size_t tab_pos = line.find('\t');
        if (tab_pos == std::string::npos) {
            // Legacy format: try space as delimiter
            tab_pos = line.find_first_of(" \t");
             if (tab_pos == std::string::npos) {
                std::cerr << "TypesDbParser: Malformed line " << line_num << " (no tab/space separator): " << line << std::endl;
                continue;
            }
        }

        std::string type_name_str = trim_string(line.substr(0, tab_pos));
        std::string ds_specs_full_str = trim_string(line.substr(tab_pos + 1));

        // Remove any trailing comments from ds_specs_full_str
        size_t comment_pos = ds_specs_full_str.find('#');
        if (comment_pos != std::string::npos) {
            ds_specs_full_str = trim_string(ds_specs_full_str.substr(0, comment_pos));
        }
        
        if (type_name_str.empty() || ds_specs_full_str.empty()) {
            std::cerr << "TypesDbParser: Malformed line " << line_num << " (empty type name or specs): " << line << std::endl;
            continue;
        }


        std::vector<std::string> ds_spec_list_str = split_string(ds_specs_full_str, ',');

        data_set_t current_ds;
        std::strncpy(current_ds.type, type_name_str.c_str(), DATA_MAX_NAME_LEN - 1);
        current_ds.type[DATA_MAX_NAME_LEN - 1] = '\0';
        current_ds.ds_num = 0;
        
        std::vector<data_source_t> sources_vec;

        for (const auto& ds_spec_str : ds_spec_list_str) {
            if (ds_spec_str.empty()) continue;

            std::vector<std::string> parts = split_string(ds_spec_str, ':');
            if (parts.size() != 4) {
                std::cerr << "TypesDbParser: Malformed data source spec on line " << line_num << ": " << ds_spec_str << std::endl;
                continue;
            }

            data_source_t src;
            std::strncpy(src.name, parts[0].c_str(), DATA_MAX_NAME_LEN - 1);
            src.name[DATA_MAX_NAME_LEN - 1] = '\0';

            src.type = parse_ds_type(parts[1]);
            if (src.type == DS_TYPE_UNDEFINED) {
                // Error already printed by parse_ds_type
                continue;
            }
            src.min = parse_ds_value(parts[2], true);
            src.max = parse_ds_value(parts[3], false);
            
            // Handle NaN from parse_ds_value if necessary, e.g., by skipping this source
            if (std::isnan(src.min) || std::isnan(src.max)) {
                 std::cerr << "TypesDbParser: Invalid min/max for data source '" << src.name << "' on line " << line_num << std::endl;
                continue;
            }

            sources_vec.push_back(src);
        }

        if (!sources_vec.empty()) {
            current_ds.ds_num = sources_vec.size();
            current_ds.ds = new data_source_t[current_ds.ds_num]; // Allocate memory
            for (size_t i = 0; i < current_ds.ds_num; ++i) {
                current_ds.ds[i] = sources_vec[i];
            }
            out_datasets.push_back(current_ds);
        } else if (!ds_spec_list_str.empty()) {
             // This means all ds_spec strings were malformed or resulted in errors
            std::cerr << "TypesDbParser: No valid data sources parsed for type '" << type_name_str << "' on line " << line_num << std::endl;
        }
    }

    infile.close();
    return 0;
}

void free_datasets(std::vector<data_set_t>& datasets) {
    for (auto& ds : datasets) {
        delete[] ds.ds; // Free the allocated array of data_source_t
        ds.ds = nullptr;
        ds.ds_num = 0;
    }
    datasets.clear();
}

}
