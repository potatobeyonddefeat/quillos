#include "filesystem.h"

namespace QuillFS {

    // Simple string helpers (no stdlib available)
    static int fs_strlen(const char* s) {
        int len = 0;
        while (s[len]) len++;
        return len;
    }

    static void fs_strcpy(char* dst, const char* src) {
        int i = 0;
        while (src[i]) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    static bool fs_strcmp(const char* a, const char* b) {
        int i = 0;
        while (a[i] && b[i]) {
            if (a[i] != b[i]) return false;
            i++;
        }
        return a[i] == b[i];
    }

    static void fs_strcat(char* dst, const char* src) {
        int len = fs_strlen(dst);
        int i = 0;
        while (src[i]) { dst[len + i] = src[i]; i++; }
        dst[len + i] = '\0';
    }

    static void fs_memset(void* dst, uint8_t val, size_t n) {
        uint8_t* p = (uint8_t*)dst;
        for (size_t i = 0; i < n; i++) p[i] = val;
    }

    bool Filesystem::init() {
        // Zero out all entries
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            fs_memset(&entries[i], 0, sizeof(FileEntry));
            entries[i].type = FILE_UNUSED;
        }

        // Create root directory
        entries[0].type = FILE_DIRECTORY;
        fs_strcpy(entries[0].name, "/");
        entries[0].parent[0] = '\0';
        entries[0].size = 0;

        return true;
    }

    int Filesystem::find_entry(const char* parent, const char* name) {
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (entries[i].type != FILE_UNUSED &&
                fs_strcmp(entries[i].name, name) &&
                fs_strcmp(entries[i].parent, parent)) {
                return (int)i;
            }
        }
        return -1;
    }

    int Filesystem::find_free_slot() {
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (entries[i].type == FILE_UNUSED) return (int)i;
        }
        return -1;
    }

    // Split "/foo/bar" into parent="/" name="bar"
    // Split "/foo/bar/baz" into parent="/foo/bar" name="baz"
    // Split "/foo" into parent="/" name="foo"
    void Filesystem::split_path(const char* path, char* parent_out, char* name_out) {
        int len = fs_strlen(path);

        // Find last '/'
        int last_slash = -1;
        for (int i = len - 1; i >= 0; i--) {
            if (path[i] == '/') { last_slash = i; break; }
        }

        if (last_slash <= 0) {
            // Parent is root
            fs_strcpy(parent_out, "/");
        } else {
            for (int i = 0; i < last_slash; i++) parent_out[i] = path[i];
            parent_out[last_slash] = '\0';
        }

        // Name is everything after last slash
        int ni = 0;
        for (int i = last_slash + 1; i < len; i++) {
            name_out[ni++] = path[i];
        }
        name_out[ni] = '\0';
    }

    bool Filesystem::path_exists(const char* parent, const char* name) {
        return find_entry(parent, name) >= 0;
    }

    bool Filesystem::mkdir(const char* path) {
        if (!path || path[0] != '/') return false;
        if (fs_strcmp(path, "/")) return false; // Root already exists

        char parent[MAX_PATH_LEN] = {0};
        char name[MAX_NAME_LEN] = {0};
        split_path(path, parent, name);

        if (fs_strlen(name) == 0) return false;

        // Check parent exists and is a directory
        // Parent "/" is always valid (entry 0)
        if (!fs_strcmp(parent, "/")) {
            // For nested paths, verify parent dir exists
            char pp[MAX_PATH_LEN] = {0};
            char pn[MAX_NAME_LEN] = {0};
            split_path(parent, pp, pn);
            int pi = find_entry(pp, pn);
            if (pi < 0 || entries[pi].type != FILE_DIRECTORY) return false;
        }

        // Check if already exists
        if (path_exists(parent, name)) return false;

        int slot = find_free_slot();
        if (slot < 0) return false;

        entries[slot].type = FILE_DIRECTORY;
        fs_strcpy(entries[slot].name, name);
        fs_strcpy(entries[slot].parent, parent);
        entries[slot].size = 0;

        return true;
    }

    bool Filesystem::touch(const char* path) {
        if (!path || path[0] != '/') return false;

        char parent[MAX_PATH_LEN] = {0};
        char name[MAX_NAME_LEN] = {0};
        split_path(path, parent, name);

        if (fs_strlen(name) == 0) return false;

        // Check parent exists
        if (!fs_strcmp(parent, "/")) {
            char pp[MAX_PATH_LEN] = {0};
            char pn[MAX_NAME_LEN] = {0};
            split_path(parent, pp, pn);
            int pi = find_entry(pp, pn);
            if (pi < 0 || entries[pi].type != FILE_DIRECTORY) return false;
        }

        // If already exists, just return true
        if (path_exists(parent, name)) return true;

        int slot = find_free_slot();
        if (slot < 0) return false;

        entries[slot].type = FILE_REGULAR;
        fs_strcpy(entries[slot].name, name);
        fs_strcpy(entries[slot].parent, parent);
        entries[slot].size = 0;
        fs_memset(entries[slot].data, 0, MAX_FILE_DATA);

        return true;
    }

    bool Filesystem::write_file(const char* path, const char* content) {
        if (!path || path[0] != '/' || !content) return false;

        char parent[MAX_PATH_LEN] = {0};
        char name[MAX_NAME_LEN] = {0};
        split_path(path, parent, name);

        int idx = find_entry(parent, name);
        if (idx < 0) {
            // Auto-create the file
            if (!touch(path)) return false;
            idx = find_entry(parent, name);
            if (idx < 0) return false;
        }

        if (entries[idx].type != FILE_REGULAR) return false;

        uint32_t len = (uint32_t)fs_strlen(content);
        if (len >= MAX_FILE_DATA) len = MAX_FILE_DATA - 1;

        fs_memset(entries[idx].data, 0, MAX_FILE_DATA);
        for (uint32_t i = 0; i < len; i++) {
            entries[idx].data[i] = (uint8_t)content[i];
        }
        entries[idx].size = len;

        return true;
    }

    bool Filesystem::read_file(const char* path, char* output_buf, uint32_t buf_size) {
        if (!path || path[0] != '/' || !output_buf) return false;

        char parent[MAX_PATH_LEN] = {0};
        char name[MAX_NAME_LEN] = {0};
        split_path(path, parent, name);

        int idx = find_entry(parent, name);
        if (idx < 0 || entries[idx].type != FILE_REGULAR) return false;

        uint32_t copy_len = entries[idx].size;
        if (copy_len >= buf_size) copy_len = buf_size - 1;

        for (uint32_t i = 0; i < copy_len; i++) {
            output_buf[i] = (char)entries[idx].data[i];
        }
        output_buf[copy_len] = '\0';

        return true;
    }

    int Filesystem::ls(const char* path, char* output_buf, uint32_t buf_size) {
        if (!path || !output_buf) return -1;

        output_buf[0] = '\0';
        int count = 0;
        uint32_t pos = 0;

        for (uint32_t i = 0; i < MAX_FILES; i++) {
            if (entries[i].type == FILE_UNUSED) continue;

            bool match = false;
            if (fs_strcmp(path, "/")) {
                match = fs_strcmp(entries[i].parent, "/");
            } else {
                match = fs_strcmp(entries[i].parent, path);
            }

            if (match) {
                // Add type indicator
                if (entries[i].type == FILE_DIRECTORY) {
                    if (pos < buf_size - 1) output_buf[pos++] = 'd';
                    if (pos < buf_size - 1) output_buf[pos++] = ' ';
                } else {
                    if (pos < buf_size - 1) output_buf[pos++] = 'f';
                    if (pos < buf_size - 1) output_buf[pos++] = ' ';
                }

                // Add name
                int nlen = fs_strlen(entries[i].name);
                for (int j = 0; j < nlen && pos < buf_size - 1; j++) {
                    output_buf[pos++] = entries[i].name[j];
                }
                if (pos < buf_size - 1) output_buf[pos++] = '\n';
                count++;
            }
        }

        output_buf[pos] = '\0';
        return count;
    }

    bool Filesystem::rm(const char* path) {
        if (!path || path[0] != '/' || fs_strcmp(path, "/")) return false;

        char parent[MAX_PATH_LEN] = {0};
        char name[MAX_NAME_LEN] = {0};
        split_path(path, parent, name);

        int idx = find_entry(parent, name);
        if (idx < 0) return false;

        // If it's a directory, check it's empty
        if (entries[idx].type == FILE_DIRECTORY) {
            // Build the full path of this directory to check children
            char full_path[MAX_PATH_LEN] = {0};
            if (fs_strcmp(parent, "/")) {
                full_path[0] = '/';
                fs_strcat(full_path, name);
            } else {
                fs_strcpy(full_path, parent);
                fs_strcat(full_path, "/");
                fs_strcat(full_path, name);
            }

            for (uint32_t i = 0; i < MAX_FILES; i++) {
                if (entries[i].type != FILE_UNUSED && fs_strcmp(entries[i].parent, full_path)) {
                    return false; // Directory not empty
                }
            }
        }

        // Clear the entry
        fs_memset(&entries[idx], 0, sizeof(FileEntry));
        entries[idx].type = FILE_UNUSED;

        return true;
    }

}
