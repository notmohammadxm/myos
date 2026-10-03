#include "fs.h"
#include <stddef.h>

struct fs_node {
    int used;
    int directory;
    char name[FS_NAME_MAX];
    uint32_t size;
    char data[FS_DATA_MAX];
};

static struct fs_node nodes[FS_MAX_FILES];
static int fs_ready;

static size_t fs_strlen(const char* s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static int fs_streq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void fs_copy(char* dst, const char* src, int cap) {
    int i = 0;
    if (!dst || cap <= 0) return;
    while (src && src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int fs_find(const char* path) {
    if (!path || !*path) return -1;
    for (int i = 0; i < FS_MAX_FILES; ++i)
        if (nodes[i].used && fs_streq(nodes[i].name, path)) return i;
    return -1;
}

static int fs_valid_path(const char* path) {
    if (!path || !*path || path[0] != '/') return 0;
    if (fs_strlen(path) >= FS_NAME_MAX) return 0;
    for (const char* p = path; *p; ++p)
        if (*p == ':' || *p == '\n' || *p == '\r') return 0;
    return 1;
}

void fs_init(void) {
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        nodes[i].used = 0;
        nodes[i].directory = 0;
        nodes[i].size = 0;
        nodes[i].name[0] = 0;
        nodes[i].data[0] = 0;
    }
    fs_ready = 1;
    nodes[0].used = 1;
    nodes[0].directory = 1;
    fs_copy(nodes[0].name, "/", FS_NAME_MAX);
    fs_create_dir("/home");
    fs_create_dir("/desktop");
    fs_create_dir("/documents");
    fs_create_dir("/downloads");
    fs_create_file("/home/readme.txt");
    fs_write("/home/readme.txt", "welcome to myos\n", 16);
    fs_create_file("/desktop/welcome.txt");
    fs_write("/desktop/welcome.txt", "myos desktop\n", 13);
}

int fs_available(void) {
    return fs_ready != 0;
}

int fs_count(void) {
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) if (nodes[i].used) ++n;
    return n;
}

int fs_list(fs_entry_t* out, int max_entries) {
    if (!fs_ready || !out || max_entries <= 0) return 0;
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES && n < max_entries; ++i) {
        if (!nodes[i].used) continue;
        out[n].index = i;
        fs_copy(out[n].name, nodes[i].name, FS_NAME_MAX);
        out[n].size = nodes[i].size;
        out[n].directory = nodes[i].directory;
        ++n;
    }
    return n;
}

int fs_create_file(const char* path) {
    if (!fs_ready || !fs_valid_path(path) || fs_find(path) >= 0) return -1;
    for (int i = 1; i < FS_MAX_FILES; ++i) {
        if (nodes[i].used) continue;
        nodes[i].used = 1;
        nodes[i].directory = 0;
        nodes[i].size = 0;
        nodes[i].name[0] = 0;
        nodes[i].data[0] = 0;
        fs_copy(nodes[i].name, path, FS_NAME_MAX);
        return i;
    }
    return -1;
}

int fs_create_dir(const char* path) {
    if (!fs_ready || !fs_valid_path(path) || fs_find(path) >= 0) return -1;
    for (int i = 1; i < FS_MAX_FILES; ++i) {
        if (nodes[i].used) continue;
        nodes[i].used = 1;
        nodes[i].directory = 1;
        nodes[i].size = 0;
        nodes[i].name[0] = 0;
        nodes[i].data[0] = 0;
        fs_copy(nodes[i].name, path, FS_NAME_MAX);
        return i;
    }
    return -1;
}

int fs_delete_index(int index) {
    if (!fs_ready || index <= 0 || index >= FS_MAX_FILES || !nodes[index].used) return 0;
    nodes[index].used = 0;
    nodes[index].directory = 0;
    nodes[index].size = 0;
    nodes[index].name[0] = 0;
    nodes[index].data[0] = 0;
    return 1;
}

int fs_delete(const char* path) {
    return fs_delete_index(fs_find(path));
}

int fs_rename(const char* old_path, const char* new_path) {
    int index = fs_find(old_path);
    if (index <= 0 || !fs_valid_path(new_path) || fs_find(new_path) >= 0) return 0;
    fs_copy(nodes[index].name, new_path, FS_NAME_MAX);
    return 1;
}

int fs_write(const char* path, const char* data, uint32_t len) {
    int index = fs_find(path);
    if (index <= 0 || nodes[index].directory || !data) return 0;
    if (len >= FS_DATA_MAX) len = FS_DATA_MAX - 1;
    for (uint32_t i = 0; i < len; ++i) nodes[index].data[i] = data[i];
    nodes[index].data[len] = 0;
    nodes[index].size = len;
    return 1;
}

int fs_read(const char* path, char* out, uint32_t cap) {
    int index = fs_find(path);
    if (index < 0 || !out || cap == 0 || nodes[index].directory) return 0;
    uint32_t n = nodes[index].size;
    if (n >= cap) n = cap - 1;
    for (uint32_t i = 0; i < n; ++i) out[i] = nodes[index].data[i];
    out[n] = 0;
    return (int)n;
}

int fs_stat(const char* path, fs_entry_t* out) {
    int index = fs_find(path);
    if (index < 0 || !out) return 0;
    out->index = index;
    fs_copy(out->name, nodes[index].name, FS_NAME_MAX);
    out->size = nodes[index].size;
    out->directory = nodes[index].directory;
    return 1;
}

const char* fs_data(int index) {
    if (index < 0 || index >= FS_MAX_FILES || !nodes[index].used || nodes[index].directory) return 0;
    return nodes[index].data;
}
