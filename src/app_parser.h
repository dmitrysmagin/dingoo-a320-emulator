#ifndef APP_PARSER_H
#define APP_PARSER_H

#include "types.h"
#include <string>
#include <vector>

struct ImportEntry {
    u32 address;
    std::string name;
};

struct ExportEntry {
    u32 address;
    std::string name;
};

struct AppBinary {
    // RAWD section
    std::vector<u8> raw_data;
    u32 raw_offset;     // file offset
    u32 raw_size;       // size in file
    u32 load_addr;      // virtual load address
    u32 prog_size;      // total program size (includes BSS)

    // Imports/exports
    std::vector<ImportEntry> imports;
    std::vector<ExportEntry> exports;

    // Resource section
    u64 resource_offset;
    u64 resource_size;

    // Entry point
    u32 entry_point;    // dl_main address
};

// Parse a Dingoo .app file
bool parse_app(const std::string& path, AppBinary& out);

#endif // APP_PARSER_H
