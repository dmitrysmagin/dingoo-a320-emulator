#include "archive.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>

// SIZED:   u32 count, 0x1FC-byte entries (0x1F4 name + u32 size + u32 data_off).
//          ERPT chunk used by PoPo Bash, AliBaba, Platinum Sudoku, ...
//          Payloads are XOR 0x40.
// REGULAR: u16 count, 0x44-byte entries (0x40 name + u32 data_off).
// PC:      u16 count, 0x24-byte entries (0x20 name + u32 data_off).
//
// Identified by first_offset == directory_size.

static const SpkFormat kSpkFormats[] = {
    {4, 0x1F4, true,  0x40, "SIZED"},
    {2, 0x40,  false, 0x00, "REGULAR"},
    {2, 0x20,  false, 0x00, "PC"},
};

static const SpkFormat* spk_detect(const u8* head, size_t head_len, u64 avail, u32* out_count) {
    for (const SpkFormat& f : kSpkFormats) {
        u32 entry_sz = spk_entry_size(f);
        if (head_len < f.count_bytes + entry_sz) continue;

        u32 count = 0;
        memcpy(&count, head, f.count_bytes);
        if (count < 1 || count > 5000) continue;

        u64 dir_size = (u64)f.count_bytes + (u64)count * entry_sz;
        if (dir_size > avail) continue;

        u32 first_off;
        memcpy(&first_off, head + f.count_bytes + f.name_len + (f.has_size ? 4 : 0), 4);
        if (first_off != dir_size) continue;

        const u8* name = head + f.count_bytes;
        u32 n = 0;
        while (n < f.name_len && name[n] >= 0x20 && name[n] <= 0x7E) n++;
        if (n == 0 || n >= f.name_len || name[n] != 0) continue;

        if (out_count) *out_count = count;
        return &f;
    }
    return nullptr;
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

    m_resource_data.resize((size_t)resource_size);
    fseek(f, (long)resource_offset, SEEK_SET);
    size_t read_bytes = fread(m_resource_data.data(), 1, (size_t)resource_size, f);
    fclose(f);

    if (read_bytes != resource_size)
        return false;

    u32 parsed_count = 0;
    const SpkFormat* fmt = spk_detect(m_resource_data.data(), m_resource_data.size(),
                                      m_resource_data.size(), &parsed_count);

    if (!fmt) {
        u32 raw_count = 0;
        memcpy(&raw_count, m_resource_data.data(), std::min<size_t>(4, m_resource_data.size()));
        printf("[ARCHIVE] Unrecognized SPK format (leading word=%u)\n", raw_count);
        m_loaded = true;
        return true;
    }

    u32 entry_size = spk_entry_size(*fmt);
    u32 name_len = fmt->name_len;
    u32 dir_size = fmt->count_bytes + parsed_count * entry_size;

    printf("[ARCHIVE] SPK format: %s (%u entries, %u-byte dir, %u-byte entries)\n",
           fmt->label, parsed_count, dir_size, entry_size);

    if (fmt->xor_key) {
        for (size_t i = dir_size; i < m_resource_data.size(); i++)
            m_resource_data[i] ^= fmt->xor_key;
        printf("[ARCHIVE] Deobfuscated payload (XOR 0x%02X)\n", fmt->xor_key);
    }

    m_entries.resize(parsed_count);

    auto lower_copy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };

    for (u32 i = 0; i < parsed_count; i++) {
        u32 entry_off = fmt->count_bytes + i * entry_size;

        u32 buf_len = std::min(name_len, 256u);
        std::vector<char> name_buf(buf_len + 1, 0);
        memcpy(name_buf.data(), &m_resource_data[entry_off], buf_len);
        name_buf[buf_len] = 0;

        u32 data_off = 0;
        memcpy(&data_off, &m_resource_data[entry_off + entry_size - 4], 4);

        std::string name(name_buf.data());
        m_entries[i].name = name;
        m_entries[i].offset = data_off;

        if (fmt->has_size) {
            memcpy(&m_entries[i].size, &m_resource_data[entry_off + name_len], 4);
        } else if (i + 1 < parsed_count) {
            u32 next_off;
            memcpy(&next_off, &m_resource_data[entry_off + entry_size + entry_size - 4], 4);
            m_entries[i].size = next_off > data_off ? next_off - data_off : 0;
        } else {
            m_entries[i].size = (u32)resource_size - data_off;
        }

        if (m_entries[i].offset > resource_size)
            m_entries[i].offset = m_entries[i].size = 0;
        else if ((u64)m_entries[i].offset + m_entries[i].size > resource_size)
            m_entries[i].size = (u32)resource_size - m_entries[i].offset;

        std::string stripped = name;
        if (name.size() >= 2 && name[0] == '.' && (name[1] == '\\' || name[1] == '/'))
            stripped = name.substr(2);

        m_name_to_index[name] = i;
        m_name_to_index[stripped] = i;

        std::string forward = stripped;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        m_name_to_index[forward] = i;

        std::string back = stripped;
        std::replace(back.begin(), back.end(), '/', '\\');
        m_name_to_index[back] = i;

        m_name_to_index[lower_copy(stripped)] = i;
        m_name_to_index[lower_copy(forward)] = i;
        m_name_to_index[lower_copy(back)] = i;
    }

    printf("[ARCHIVE] Loaded %u entries from SPK archive\n", parsed_count);
    m_loaded = true;
    return true;
}

const ArchiveEntry* Archive::find(const std::string& path) const {
    if (!m_loaded) return nullptr;

    auto try_key = [&](const std::string& key) -> const ArchiveEntry* {
        auto it = m_name_to_index.find(key);
        if (it != m_name_to_index.end())
            return &m_entries[it->second];
        std::string lkey = key;
        std::transform(lkey.begin(), lkey.end(), lkey.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        it = m_name_to_index.find(lkey);
        if (it != m_name_to_index.end())
            return &m_entries[it->second];
        return nullptr;
    };

    if (auto* e = try_key(path)) return e;

    std::string stripped = path;
    if (stripped.size() >= 2 && stripped[0] == '.' && (stripped[1] == '\\' || stripped[1] == '/'))
        stripped = stripped.substr(2);
    if (auto* e = try_key(stripped)) return e;
    if (auto* e = try_key(".\\" + stripped)) return e;
    if (auto* e = try_key("./" + stripped)) return e;

    size_t slash = stripped.find_last_of("/\\");
    if (slash != std::string::npos) {
        if (auto* e = try_key(stripped.substr(slash + 1))) return e;
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
