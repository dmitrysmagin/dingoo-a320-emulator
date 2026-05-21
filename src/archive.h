#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "types.h"
#include <vector>
#include <string>
#include <unordered_map>

constexpr u32 RESOURCE_SIZE = 0x323E7EB;

struct ArchiveEntry {
    std::string name;  // full path like ".\common\hv_guzi_w.stx"
    u32 offset;        // offset within the resource section
    u32 size;          // file size
};

class Archive {
public:
    Archive();
    ~Archive();

    bool load(const std::string& app_path);

    // Find file by path (matches both "path" and ".\path" forms)
    const ArchiveEntry* find(const std::string& path) const;

    // Read file data into buffer. Returns bytes read, 0 if not found.
    u32 read(const ArchiveEntry& entry, u8* buffer, u32 offset, u32 size) const;

    // Get raw pointer to file data (for direct memory access)
    const u8* get_data(const ArchiveEntry& entry) const;

    // Total entries
    size_t count() const { return m_entries.size(); }

private:
    std::vector<ArchiveEntry> m_entries;
    std::unordered_map<std::string, size_t> m_name_to_index;
    std::vector<u8> m_resource_data;
    bool m_loaded;
};

#endif // ARCHIVE_H
