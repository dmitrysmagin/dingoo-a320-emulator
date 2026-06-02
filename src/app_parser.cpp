#include "app_parser.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// Fixed-size header structs (matching SDK)
#pragma pack(push, 1)

struct CCDLHeader {
    char ident[4];        // "CCDL"
    uint8_t unknown[20];
    uint8_t padding[8];
};

struct IMPTHeader {
    char ident[4];        // "IMPT"
    uint32_t unknown;
    uint32_t offset;      // file offset of import table
    uint32_t size;        // size of import table
    uint8_t padding[16];
};

struct EXPTHeader {
    char ident[4];        // "EXPT"
    uint32_t unknown;
    uint32_t offset;      // file offset of export table
    uint32_t size;        // size of export table
    uint8_t padding[16];
};

struct RAWDHeader {
    char ident[4];        // "RAWD"
    uint32_t unknown0;
    uint32_t offset;      // file offset of binary data
    uint32_t size;        // size of binary data
    uint32_t unknown1;
    uint32_t entry;       // entry point (dl_main)
    uint32_t origin;      // load address (0x80A00000)
    uint32_t prog_size;   // total program size (including BSS)
};

// Import/export table entry
struct TableEntry {
    uint32_t str_offset;  // cumulative string offset
    uint32_t unknown[2];
    uint32_t offset;      // address in binary
};

struct TableHeader {
    uint32_t count;
    uint32_t unknown[2];
    uint32_t str_base;
};

#pragma pack(pop)

bool parse_app(const std::string& path, AppBinary& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[APP] Cannot open: %s\n", path.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read fixed headers (4 × 32 bytes = 128 bytes)
    CCDLHeader ccdl;
    IMPTHeader impt;
    EXPTHeader expt;
    RAWDHeader rawd;

    if (fread(&ccdl, sizeof(ccdl), 1, f) != 1 ||
        fread(&impt, sizeof(impt), 1, f) != 1 ||
        fread(&expt, sizeof(expt), 1, f) != 1 ||
        fread(&rawd, sizeof(rawd), 1, f) != 1) {
        fprintf(stderr, "[APP] Failed to read headers\n");
        fclose(f);
        return false;
    }

    // Verify headers
    if (memcmp(ccdl.ident, "CCDL", 4) != 0) {
        fprintf(stderr, "[APP] Invalid CCDL header\n");
        fclose(f);
        return false;
    }
    if (memcmp(impt.ident, "IMPT", 4) != 0) {
        fprintf(stderr, "[APP] Invalid IMPT header\n");
        fclose(f);
        return false;
    }
    if (memcmp(expt.ident, "EXPT", 4) != 0) {
        fprintf(stderr, "[APP] Invalid EXPT header\n");
        fclose(f);
        return false;
    }
    if (memcmp(rawd.ident, "RAWD", 4) != 0) {
        fprintf(stderr, "[APP] Invalid RAWD header\n");
        fclose(f);
        return false;
    }

    printf("[APP] File size: %ld bytes (0x%lX)\n", file_size, file_size);
    printf("[APP] IMPT: offset=0x%X size=0x%X\n", impt.offset, impt.size);
    printf("[APP] EXPT: offset=0x%X size=0x%X\n", expt.offset, expt.size);
    printf("[APP] RAWD: offset=0x%X size=0x%X entry=0x%08X origin=0x%08X prog_size=0x%X\n",
           rawd.offset, rawd.size, rawd.entry, rawd.origin, rawd.prog_size);

    // Read import table
    fseek(f, impt.offset, SEEK_SET);
    TableHeader impt_hdr;
    fread(&impt_hdr, sizeof(impt_hdr), 1, f);

    printf("[APP] Imports: %u entries\n", impt_hdr.count);

    std::vector<TableEntry> impt_entries(impt_hdr.count);
    fread(impt_entries.data(), sizeof(TableEntry), impt_hdr.count, f);

    // Read import strings (immediately after entries)
    u32 str_table_offset = impt.offset + sizeof(TableHeader) + impt_hdr.count * sizeof(TableEntry);
    u32 str_data_size = impt.size - (str_table_offset - impt.offset);
    std::vector<char> impt_strings(str_data_size + 1, 0);
    fseek(f, str_table_offset, SEEK_SET);
    fread(impt_strings.data(), 1, str_data_size, f);

    // Build import entries using str_offset from each entry
    for (u32 i = 0; i < impt_hdr.count; i++) {
        ImportEntry entry;
        entry.address = impt_entries[i].offset;
        u32 str_pos = impt_entries[i].str_offset;
        if (str_pos < impt_strings.size()) {
            size_t len = 0;
            while (str_pos + len < impt_strings.size() && impt_strings[str_pos + len] != '\0') len++;
            entry.name = std::string(&impt_strings[str_pos], len);
        }
        out.imports.push_back(entry);
    }

    // Read export table
    fseek(f, expt.offset, SEEK_SET);
    TableHeader expt_hdr;
    fread(&expt_hdr, sizeof(expt_hdr), 1, f);

    printf("[APP] Exports: %u entries\n", expt_hdr.count);

    std::vector<TableEntry> expt_entries(expt_hdr.count);
    fread(expt_entries.data(), sizeof(TableEntry), expt_hdr.count, f);

    // Read export strings
    u32 expt_str_table_offset = expt.offset + sizeof(TableHeader) + expt_hdr.count * sizeof(TableEntry);
    u32 expt_str_data_size = expt.size - (expt_str_table_offset - expt.offset);
    std::vector<char> expt_strings(expt_str_data_size + 1, 0);
    fseek(f, expt_str_table_offset, SEEK_SET);
    fread(expt_strings.data(), 1, expt_str_data_size, f);

    // Build export entries using str_offset from each entry
    for (u32 i = 0; i < expt_hdr.count; i++) {
        ExportEntry entry;
        entry.address = expt_entries[i].offset;
        u32 str_pos = expt_entries[i].str_offset;
        if (str_pos < expt_strings.size()) {
            size_t len = 0;
            while (str_pos + len < expt_strings.size() && expt_strings[str_pos + len] != '\0') len++;
            entry.name = std::string(&expt_strings[str_pos], len);
        }
        out.exports.push_back(entry);
    }

    // Read RAWD binary data
    out.raw_offset = rawd.offset;
    out.raw_size = rawd.size;
    out.load_addr = rawd.origin;
    out.prog_size = rawd.prog_size;
    out.entry_point = rawd.entry;

    out.raw_data.resize(rawd.size);
    fseek(f, rawd.offset, SEEK_SET);
    if (fread(out.raw_data.data(), 1, rawd.size, f) != rawd.size) {
        fprintf(stderr, "[APP] Failed to read RAWD data\n");
        fclose(f);
        return false;
    }

    printf("[APP] RAWD loaded: %u bytes\n", rawd.size);

    // Resource section — dynamically locate the SPK archive
    u64 raopp  = (u64)rawd.offset + rawd.prog_size;
    u64 rawd_end = (u64)rawd.offset + rawd.size;

    // SPK candidate offsets: try aligned + raw end-of-RAWD positions
    u64 spk_candidates[] = {
        ((raopp + 0x7FFFF) & ~0x7FFFFu),   // align(raopp, 0x80000)
        ((raopp + 0xFFFF)  & ~0xFFFFu),     // align(raopp, 0x10000)
        ((rawd_end + 0x7FFFF) & ~0x7FFFFu), // align(rawd_end, 0x80000)
        ((rawd_end + 0xFFFF)  & ~0xFFFFu),  // align(rawd_end, 0x10000)
        rawd_end,                           // right after code
        rawd_end + 8,                       // after padding
    };

    auto spk_validate = [&](u64 off) -> bool {
        if (off + 2 > (u64)file_size) return false;

        // Try u16-count formats: REGULAR (0x44) and PC (0x24)
        fseek(f, (long)off, SEEK_SET);
        u16 count_u16;
        if (fread(&count_u16, 2, 1, f) != 1) return false;
        if (count_u16 >= 1 && count_u16 <= 5000) {
            for (u32 entry_sz : {0x44u, 0x24u}) {
                u32 dir_sz = 2 + (u32)count_u16 * entry_sz;
                if (off + dir_sz > (u64)file_size) continue;
                bool valid = true;
                bool found_nonzero = false;
                u32 first_do = 0;
                u32 max_check = std::min((u32)count_u16, 3u);
                for (u32 i = 0; i < max_check; i++) {
                    u32 data_off;
                    fseek(f, (long)(off + 2 + i * entry_sz + entry_sz - 4), SEEK_SET);
                    if (fread(&data_off, 4, 1, f) != 1) { valid = false; break; }
                    if (data_off == 0) continue;  // sentinel entry, skip
                    found_nonzero = true;
                    u64 abs_doff = off + data_off;
                    if (abs_doff < off + dir_sz || abs_doff > (u64)file_size)
                        { valid = false; break; }
                    if (i == 0) first_do = data_off;
                    else if (data_off == first_do) { valid = false; break; } // reject all-identical
                }
                if (valid && found_nonzero) return true;
            }
        }

        // Try BIGNAME format: u32 count, entry_size = 0x1F8 (0x1F4 name + 4 offset)
        if (off + 4 > (u64)file_size) return false;
        fseek(f, (long)off, SEEK_SET);  // rewind to start for u32 read
        u32 count_u32;
        if (fread(&count_u32, 4, 1, f) != 1) return false;
        if (count_u32 >= 1 && count_u32 <= 5000) {
            u32 entry_sz = 0x1F8;
            u32 dir_sz = 4 + count_u32 * entry_sz;
            if (off + dir_sz <= (u64)file_size) {
                bool valid = true;
                bool found_nonzero = false;
                u32 first_do = 0;
                u32 max_check = std::min(count_u32, 3u);
                for (u32 i = 0; i < max_check; i++) {
                    u32 data_off;
                    fseek(f, (long)(off + 4 + i * entry_sz + entry_sz - 4), SEEK_SET);
                    if (fread(&data_off, 4, 1, f) != 1) { valid = false; break; }
                    if (data_off == 0) continue;  // sentinel entry
                    found_nonzero = true;
                    u64 abs_doff = off + data_off;
                    if (abs_doff < off + dir_sz || abs_doff > (u64)file_size)
                        { valid = false; break; }
                    if (i == 0) first_do = data_off;
                    else if (data_off == first_do) { valid = false; break; }
                }
                if (valid && found_nonzero) return true;
            }
        }

        return false;
    };

    out.resource_offset = 0;
    for (u64 off : spk_candidates) {
        if (!spk_validate(off)) continue;
        out.resource_offset = off;
        // Read count for display
        fseek(f, (long)off, SEEK_SET);
        u16 count;
        fread(&count, 2, 1, f);
        printf("[APP] SPK archive at 0x%llX (%u entries)\n", off, count);
        break;
    }

    if (out.resource_offset == 0) {
        // Fallback: scan at finer granularity
        for (u64 off = spk_candidates[0]; off > rawd_end && off > 0; off -= 0x10000) {
            if (spk_validate(off)) {
                out.resource_offset = off;
                fseek(f, (long)off, SEEK_SET);
                u16 count;
                fread(&count, 2, 1, f);
                printf("[APP] SPK archive at 0x%llX (%u entries, fallback)\n", off, count);
                break;
            }
        }
    }

    if (out.resource_offset == 0) {
        out.resource_offset = spk_candidates[0];
        fprintf(stderr, "[APP] Warning: could not validate SPK, using 0x%llX\n", out.resource_offset);
    }

    if (out.resource_offset == 0) {
        out.resource_offset = spk_candidates[0];
        fprintf(stderr, "[APP] Warning: could not validate SPK, using 0x%llX\n", out.resource_offset);
    }

    out.resource_size = (u64)file_size - out.resource_offset;
    printf("[APP] Resource section: offset=0x%llX size=0x%llX\n",
           out.resource_offset, out.resource_size);

    fclose(f);
    return true;
}
