#include "archive.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cassert>

// SPK archive format — two variants:
//
// Regular (Dingoo): 0x44-byte entries, 64-byte name + u32 data_off, zero-padded.
//   Used by 7days, ultimate_drift.
//
// PC version:        0x24-byte entries, 32-byte name + u32 data_off, 0xCD-padded.
//   Used by tetris, brick, candy.
//
// Detection: try REGULAR first (validates first 3 entries' data offsets),
// fall back to PC if entries don't validate.

static constexpr u32 SPK_ENTRY_REG = 0x44;   // 68 bytes
static constexpr u32 SPK_ENTRY_PC  = 0x24;   // 36 bytes

Archive::Archive() : m_loaded(false) {}

Archive::~Archive() {}

bool Archive::load(const std::string& app_path, u64 resource_offset, u64 resource_size) {
    FILE* f = fopen(app_path.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    u64 total_size = ftell(f);
    if (resource_offset + resource_size > total_size) {
        resource_size = total_size > resource_offset ? total_size - resource_offset : 0;
    }
    if (resource_size == 0) {
        fclose(f);
        return false;
    }

    // Read the entire resource section
    m_resource_data.resize((size_t)resource_size);
    fseek(f, (long)resource_offset, SEEK_SET);
    size_t read_bytes = fread(m_resource_data.data(), 1, (size_t)resource_size, f);
    fclose(f);

    if (read_bytes != resource_size)
        return false;

    if (m_resource_data.size() < 2) { m_loaded = true; return true; }

    u16 entry_count;
    memcpy(&entry_count, m_resource_data.data(), 2);

    // Detect SPK format by validating data offsets for first 3 entries
    u32 entry_size = 0;
    for (u32 try_sz : {SPK_ENTRY_REG, SPK_ENTRY_PC}) {
        u32 dir_sz = 2 + (u32)entry_count * try_sz;
        if (dir_sz > m_resource_data.size()) continue;

        bool valid = true;
        u32 max_check = std::min((u32)entry_count, 3u);
        for (u32 i = 0; i < max_check; i++) {
            u32 data_off;
            memcpy(&data_off, &m_resource_data[2 + i * try_sz + try_sz - 4], 4);
            if (data_off < dir_sz || (u64)data_off + 4 > m_resource_data.size()) {
                valid = false;
                break;
            }
        }
        if (valid) {
            entry_size = try_sz;
            break;
        }
    }

    if (entry_size == 0) {
        printf("[ARCHIVE] Unrecognized SPK format (count=%u)\n", (u32)entry_count);
        m_loaded = true;
        return true;
    }

    const char* label = (entry_size == SPK_ENTRY_REG) ? "REGULAR" : "PC";
    printf("[ARCHIVE] SPK format: %s (%u entries, %u-byte dir)\n",
           label, (u32)entry_count, 2 + (u32)entry_count * entry_size);

    u32 name_len = entry_size - 4;
    u32 dir_size = 2 + (u32)entry_count * entry_size;

    m_entries.reserve(entry_count);
    m_entries.resize(entry_count);

    for (u32 i = 0; i < (u32)entry_count; i++) {
        u32 entry_off = 2 + i * entry_size;

        // Read name (null-terminated, padded with 0x00 or 0xCD)
        char name_buf[65] = {};
        memcpy(name_buf, &m_resource_data[entry_off], std::min(name_len, 64u));
        // Strip padding after first null
        for (u32 j = 0; j < std::min(name_len, 64u); j++) {
            if (name_buf[j] == 0) { name_buf[j] = 0; break; }
        }

        // Read 4-byte data offset
        u32 data_off;
        memcpy(&data_off, &m_resource_data[entry_off + name_len], 4);

        std::string name(name_buf);
        m_entries[i].name = name;
        m_entries[i].offset = data_off;

        // Compute file size from next entry's offset (or end of resource)
        if (i + 1 < (u32)entry_count) {
            u32 next_off;
            memcpy(&next_off, &m_resource_data[2 + (i + 1) * entry_size + name_len], 4);
            m_entries[i].size = next_off - data_off;
        } else {
            m_entries[i].size = (u32)resource_size - data_off;
        }

        // Store in hash table by various path forms
        std::string stripped;
        if (name.size() >= 2 && name[0] == '.' && (name[1] == '\\' || name[1] == '/'))
            stripped = name.substr(2);
        else
            stripped = name;

        m_name_to_index[name] = i;
        m_name_to_index[stripped] = i;

        // Forward slashes
        std::string forward = stripped;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        m_name_to_index[forward] = i;

        // Also store with backslash version
        std::string back = stripped;
        std::replace(back.begin(), back.end(), '/', '\\');
        m_name_to_index[back] = i;

        // Lowercase version
        std::string lower = stripped;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        m_name_to_index[lower] = i;
    }

    printf("[ARCHIVE] Loaded %u entries from SPK archive\n", (u32)entry_count);
    m_loaded = true;
    return true;
}

const ArchiveEntry* Archive::find(const std::string& path) const {
    if (!m_loaded) return nullptr;

    // Build lowercase version of the query
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::replace(lower.begin(), lower.end(), '/', '\\');

    auto try_key = [&](const std::string& key) -> const ArchiveEntry* {
        auto it = m_name_to_index.find(key);
        if (it != m_name_to_index.end())
            return &m_entries[it->second];
        std::string lkey = key;
        std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);
        it = m_name_to_index.find(lkey);
        if (it != m_name_to_index.end())
            return &m_entries[it->second];
        return nullptr;
    };

    if (auto* e = try_key(path)) return e;

    // Try with .\ prefix if not already present
    if (path.size() < 2 || path[0] != '.') {
        if (auto* e = try_key(".\\" + path)) return e;
        if (auto* e = try_key("./" + path)) return e;
    }

    return nullptr;
}

u32 Archive::read(const ArchiveEntry& entry, u8* buffer, u32 offset, u32 size) const {
    if (offset >= entry.size) return 0;
    u32 actual = std::min(size, entry.size - offset);
    memcpy(buffer, m_resource_data.data() + entry.offset + offset, actual);
    return actual;
}

const u8* Archive::get_data(const ArchiveEntry& entry) const {
    return m_resource_data.data() + entry.offset;
}
