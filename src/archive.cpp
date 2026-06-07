#include "archive.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <vector>

// SPK archive format — three variants:
//
// Regular (Dingoo): 0x44-byte entries, 64-byte name + u32 data_off, zero-padded.
//   Used by 7days, ultimate_drift.
//
// PC version:        0x24-byte entries, 32-byte name + u32 data_off, 0xCD-padded.
//   Used by tetris, brick, candy.
//
// Big-name version:  u32 entry_count (4 bytes), 0x1F4-byte name + u32 data_off per entry.
//   Used by Puzzle Bobble - Popo Bash (Chinese).
//
// Interleaved (Landlord): u32 count + 0x1FC-byte entries (0x1F4 name + u32 size + u32 data_off).
//   Data blobs live anywhere in the file; offsets are relative to the archive start.
//   Used by Landlord.app and similar Chinese SDK games.
//
// Detection: try each variant in order, validating first 3 entries' data offsets.

static constexpr u32 SPK_ENTRY_REG         = 0x44;   // 68 bytes
static constexpr u32 SPK_ENTRY_PC          = 0x24;   // 36 bytes
static constexpr u32 SPK_ENTRY_BIGNAME     = 0x1F8;  // 504 bytes (0x1F4 name + 4 offset)
static constexpr u32 SPK_ENTRY_INTERLEAVED = 0x1FC; // 508 bytes (0x1F4 name + 4 offset + 4 pad)
static constexpr u32 SPK_NAME_INTERLEAVED  = 0x1F4;

static bool spk_name_looks_like_path(const u8* name, u32 len) {
    u32 end = 0;
    while (end < len && name[end]) end++;
    if (end < 3) return false;
    for (u32 i = 0; i < end; i++) {
        if (name[i] == '\\' || name[i] == '/') return true;
    }
    return false;
}

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
    struct SpkCandidate { u32 entry_sz; u32 count_bytes; u32 name_len; bool interleaved; const char* label; };
    SpkCandidate candidates[] = {
        {SPK_ENTRY_REG, 2, SPK_ENTRY_REG - 4, false, "REGULAR"},
        {SPK_ENTRY_PC, 2, SPK_ENTRY_PC - 4, false, "PC"},
        {SPK_ENTRY_INTERLEAVED, 4, SPK_NAME_INTERLEAVED, true, "INTERLEAVED"},
        {SPK_ENTRY_BIGNAME, 4, SPK_ENTRY_BIGNAME - 4, false, "BIGNAME"},
    };

    u32 entry_size = 0;
    u32 count_bytes = 2;
    u32 name_len_field = 0;
    bool interleaved = false;
    const char* spk_label = "";
    u32 parsed_count = entry_count;

    for (auto& c : candidates) {
        u32 ec = 0;
        if (c.count_bytes == 2) {
            memcpy(&ec, m_resource_data.data(), 2);
        } else {
            if (m_resource_data.size() < 4) continue;
            memcpy(&ec, m_resource_data.data(), 4);
        }
        if (ec < 1 || ec > 5000) continue;
        u32 dir_sz = c.count_bytes + ec * c.entry_sz;
        if (dir_sz > m_resource_data.size()) continue;

        bool valid = true;
        u32 first_do = 0;
        u32 max_check = std::min(ec, 3u);
        bool found_nonzero = false;
        for (u32 i = 0; i < max_check; i++) {
            u32 entry_off = c.count_bytes + i * c.entry_sz;
            if (c.interleaved && !spk_name_looks_like_path(&m_resource_data[entry_off], c.name_len)) {
                valid = false;
                break;
            }
            u32 off = entry_off + c.name_len;
            if (c.interleaved) {
                if (off + 8 > m_resource_data.size()) { valid = false; break; }
                u32 data_size, data_off;
                memcpy(&data_size, &m_resource_data[off], 4);
                memcpy(&data_off, &m_resource_data[off + 4], 4);
                if (data_off == 0) continue;
                if (data_size == 0 || data_off < dir_sz ||
                    (u64)data_off + data_size > m_resource_data.size()) {
                    valid = false;
                    break;
                }
                found_nonzero = true;
            } else {
                u32 data_off;
                if (off + 4 > m_resource_data.size()) { valid = false; break; }
                memcpy(&data_off, &m_resource_data[off], 4);
                if (data_off == 0) continue;
                if (data_off < dir_sz || (u64)data_off + 4 > m_resource_data.size()) {
                    valid = false;
                    break;
                }
                if (!found_nonzero) { first_do = data_off; found_nonzero = true; }
                else if (data_off == first_do) { valid = false; break; }
            }
        }
        if (valid && found_nonzero) {
            entry_size = c.entry_sz;
            count_bytes = c.count_bytes;
            name_len_field = c.name_len;
            interleaved = c.interleaved;
            parsed_count = ec;
            spk_label = c.label;
            break;
        }
    }

    if (entry_size == 0) {
        printf("[ARCHIVE] Unrecognized SPK format (count=%u)\n", (u32)entry_count);
        m_loaded = true;
        return true;
    }

    printf("[ARCHIVE] SPK format: %s (%u entries, %u-byte dir, %u-byte entries)\n",
           spk_label, parsed_count, count_bytes + parsed_count * entry_size, entry_size);

    u32 name_len = name_len_field ? name_len_field : (entry_size - 4);

    m_entries.reserve(parsed_count);
    m_entries.resize(parsed_count);

    for (u32 i = 0; i < parsed_count; i++) {
        u32 entry_off = count_bytes + i * entry_size;

        // Read name (null-terminated, padded with 0x00 or 0xCD)
        u32 buf_len = std::min(name_len, 256u);
        std::vector<char> name_buf(buf_len + 1, 0);
        memcpy(name_buf.data(), &m_resource_data[entry_off], buf_len);
        name_buf[buf_len] = 0;

        u32 field_off = entry_off + name_len;
        u32 data_size = 0;
        u32 data_off = 0;
        if (interleaved) {
            memcpy(&data_size, &m_resource_data[field_off], 4);
            memcpy(&data_off, &m_resource_data[field_off + 4], 4);
        } else {
            memcpy(&data_off, &m_resource_data[field_off], 4);
        }

        std::string name(name_buf.data());
        m_entries[i].name = name;
        m_entries[i].offset = data_off;

        if (interleaved) {
            m_entries[i].size = data_size;
        } else if (i + 1 < parsed_count) {
            u32 next_off;
            memcpy(&next_off, &m_resource_data[count_bytes + (i + 1) * entry_size + name_len], 4);
            m_entries[i].size = next_off > data_off ? next_off - data_off : 0;
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

    printf("[ARCHIVE] Loaded %u entries from SPK archive\n", parsed_count);
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
