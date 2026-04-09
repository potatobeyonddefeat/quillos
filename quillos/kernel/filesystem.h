#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>

namespace QuillFS {

    static constexpr uint32_t MAX_FILES = 64;
    static constexpr uint32_t MAX_NAME_LEN = 32;
    static constexpr uint32_t MAX_PATH_LEN = 128;
    static constexpr uint32_t MAX_FILE_DATA = 1024;

    enum FileType : uint8_t {
        FILE_UNUSED = 0,
        FILE_REGULAR = 1,
        FILE_DIRECTORY = 2,
    };

    struct FileEntry {
        char name[MAX_NAME_LEN];
        char parent[MAX_PATH_LEN];
        FileType type;
        uint8_t data[MAX_FILE_DATA];
        uint32_t size;
    };

    class Filesystem {
    public:
        Filesystem() = default;
        bool init();

        // Directory operations
        bool mkdir(const char* path);
        int ls(const char* path, char* output_buf, uint32_t buf_size);

        // File operations
        bool touch(const char* path);
        bool write_file(const char* path, const char* content);
        bool read_file(const char* path, char* output_buf, uint32_t buf_size);
        bool rm(const char* path);

    private:
        FileEntry entries[MAX_FILES];
        int find_entry(const char* parent, const char* name);
        int find_free_slot();
        void split_path(const char* path, char* parent_out, char* name_out);
        bool path_exists(const char* parent, const char* name);
    };

}

#endif
