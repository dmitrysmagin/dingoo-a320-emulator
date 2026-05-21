#include "archive.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cassert>

// SPK archive format:
//   uint16 LE: entry_count
//   entry[0..entry_count-1]:
//     char[64]:  name (null-padded, ASCII, e.g. ".\audio\file.sau")
//     uint32 LE: data_offset (relative to start of resource section)
//   data_block: starts at offset 2 + entry_count * 68

Archive::Archive() : m_loaded(false) {}

Archive::~Archive() {}

bool Archive::load(const std::string& app_path) {
    FILE* f = fopen(app_path.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    u32 total_size = ftell(f);
    if (RESOURCE_OFFSET + RESOURCE_SIZE > total_size) {
        fclose(f);
        return false;
    }

    // Read the entire resource section
    m_resource_data.resize(RESOURCE_SIZE);
    fseek(f, RESOURCE_OFFSET, SEEK_SET);
    size_t read_bytes = fread(m_resource_data.data(), 1, RESOURCE_SIZE, f);
    fclose(f);

    if (read_bytes != RESOURCE_SIZE)
        return false;

    // Parse SPK directory
    if (m_resource_data.size() < 2) { m_loaded = true; return true; }

    u16 entry_count;
    memcpy(&entry_count, m_resource_data.data(), 2);

    u32 dir_size = 2 + (u32)entry_count * 68;
    if (dir_size > m_resource_data.size()) {
        printf("[ARCHIVE] Invalid entry count %u\n", (u32)entry_count);
        m_loaded = true;
        return true;
    }

    m_entries.reserve(entry_count);
    m_entries.resize(entry_count);

    for (u32 i = 0; i < (u32)entry_count; i++) {
        u32 entry_off = 2 + i * 68;

        // Read 64-byte name, null-terminated
        char name_buf[65];
        memcpy(name_buf, &m_resource_data[entry_off], 64);
        name_buf[64] = 0;

        // Read 4-byte data offset
        u32 data_off;
        memcpy(&data_off, &m_resource_data[entry_off + 64], 4);

        // Strip leading ".\" or ".\" from name if present
        std::string name(name_buf);
        m_entries[i].name = name;
        m_entries[i].offset = data_off;

        // Compute file size from next entry's offset (or end of resource)
        if (i + 1 < (u32)entry_count) {
            u32 next_off;
            memcpy(&next_off, &m_resource_data[2 + (i + 1) * 68 + 64], 4);
            m_entries[i].size = next_off - data_off;
        } else {
            m_entries[i].size = RESOURCE_SIZE - data_off;
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

    auto it = m_name_to_index.find(path);
    if (it != m_name_to_index.end()) {
        return &m_entries[it->second];
    }

    // Try with .\ prefix if not already present
    std::string with_prefix;
    if (path.size() < 2 || path[0] != '.') {
        with_prefix = ".\\" + path;
        auto it2 = m_name_to_index.find(with_prefix);
        if (it2 != m_name_to_index.end())
            return &m_entries[it2->second];
    }

    // Try ./ prefix
    std::string with_slash_prefix;
    if (path.size() < 2 || path[0] != '.') {
        with_slash_prefix = "./" + path;
        auto it3 = m_name_to_index.find(with_slash_prefix);
        if (it3 != m_name_to_index.end())
            return &m_entries[it3->second];
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
