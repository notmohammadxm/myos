#include "fs.h"
#include "../storage/ata.h"
#include <stddef.h>

#define FS_DISK_MAGIC 0x4D594653u
#define FS_DISK_VERSION 1u
#define FS_SUPER_LBA 16u
#define FS_DIR_LBA 17u
#define FS_DATA_LBA 32u
#define FS_DISK_SECTORS 4u

struct fs_node {
    int used;
    int directory;
    char name[FS_NAME_MAX];
    uint32_t size;
    char data[FS_DATA_MAX];
};

static struct fs_node nodes[FS_MAX_FILES];
static int fs_ready;
static int fs_persistent;

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

static uint32_t fs_get32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void fs_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t fs_checksum(const uint8_t* p, int length) {
    uint32_t sum = 2166136261u;
    for (int i = 0; i < length; ++i) {
        sum ^= p[i];
        sum *= 16777619u;
    }
    return sum;
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
    if (fs_streq(path, "/")) return 1;
    if (path[1] == '/' || path[fs_strlen(path) - 1] == '/') return 0;
    for (const char* p = path; *p; ++p)
        if (*p == ':' || *p == '\n' || *p == '\r') return 0;
    return 1;
}

static int fs_parent_exists(const char* path) {
    if (!path || path[0] != '/' || fs_streq(path, "/")) return 0;
    int last = -1;
    for (int i = 1; path[i]; ++i)
        if (path[i] == '/') last = i;
    if (last <= 0) return fs_find("/") == 0;
    char parent[FS_NAME_MAX];
    int n = last;
    if (n >= (int)sizeof(parent)) return 0;
    for (int i = 0; i < n; ++i) parent[i] = path[i];
    parent[n] = 0;
    int index = fs_find(parent);
    return index >= 0 && nodes[index].directory;
}

static int fs_dir_empty(int index) {
    if (index <= 0 || index >= FS_MAX_FILES || !nodes[index].used || !nodes[index].directory) return 0;
    int prefix_len = (int)fs_strlen(nodes[index].name);
    for (int i = 1; i < FS_MAX_FILES; ++i) {
        if (!nodes[i].used || i == index) continue;
        if (nodes[i].name[0] != '/') continue;
        int j = 0;
        while (j < prefix_len && nodes[i].name[j] == nodes[index].name[j]) ++j;
        if (j == prefix_len && nodes[i].name[prefix_len] == '/')
            return 0;
    }
    return 1;
}

static void fs_reset(void) {
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        nodes[i].used = 0;
        nodes[i].directory = 0;
        nodes[i].size = 0;
        nodes[i].name[0] = 0;
        nodes[i].data[0] = 0;
    }
    nodes[0].used = 1;
    nodes[0].directory = 1;
    fs_copy(nodes[0].name, "/", FS_NAME_MAX);
}

static int fs_disk_geometry_ok(void) {
    return ata_available() && ata_sector_count() >= FS_DATA_LBA + FS_MAX_FILES;
}

static int fs_load_disk(void) {
    if (!fs_disk_geometry_ok()) return 0;
    uint8_t super[512];
    uint8_t dir[FS_DISK_SECTORS * 512u];
    if (!ata_read_sector(FS_SUPER_LBA, super)) return 0;
    if (fs_get32(super) != FS_DISK_MAGIC || super[4] != FS_DISK_VERSION) return 0;
    uint32_t stored = fs_get32(super + 8);
    uint8_t sum_block[512];
    for (int i = 0; i < 512; ++i) sum_block[i] = super[i];
    sum_block[8] = sum_block[9] = sum_block[10] = sum_block[11] = 0;
    if (fs_checksum(sum_block, 512) != stored) return 0;
    for (uint32_t s = 0; s < FS_DISK_SECTORS; ++s)
        if (!ata_read_sector(FS_DIR_LBA + s, dir + s * 512u)) return 0;

    fs_reset();
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        const uint8_t* e = dir + i * 64u;
        nodes[i].used = e[0] ? 1 : 0;
        nodes[i].directory = e[1] ? 1 : 0;
        for (int j = 0; j < FS_NAME_MAX; ++j) nodes[i].name[j] = (char)e[2 + j];
        nodes[i].name[FS_NAME_MAX - 1] = 0;
        nodes[i].size = fs_get32(e + 50);
        if (nodes[i].size >= FS_DATA_MAX) nodes[i].size = FS_DATA_MAX - 1;
        if (!nodes[i].used) {
            nodes[i].name[0] = 0;
            nodes[i].size = 0;
            continue;
        }
        nodes[i].data[0] = 0;
        if (!nodes[i].directory) {
            uint8_t data[512];
            if (!ata_read_sector(FS_DATA_LBA + (uint32_t)i, data)) return 0;
            for (uint32_t j = 0; j < nodes[i].size; ++j) nodes[i].data[j] = (char)data[j];
            nodes[i].data[nodes[i].size] = 0;
        }
    }
    if (!nodes[0].used || !nodes[0].directory || !fs_streq(nodes[0].name, "/"))
        return 0;
    return 1;
}

static int fs_sync_disk(void) {
    if (!fs_persistent || !fs_disk_geometry_ok()) return 0;
    uint8_t dir[FS_DISK_SECTORS * 512u];
    uint8_t super[512];
    for (int i = 0; i < (int)sizeof(dir); ++i) dir[i] = 0;
    for (int i = 0; i < 512; ++i) super[i] = 0;

    fs_put32(super, FS_DISK_MAGIC);
    super[4] = FS_DISK_VERSION;
    super[5] = 1;
    fs_put32(super + 12, (uint32_t)fs_count());

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        uint8_t* e = dir + i * 64u;
        e[0] = nodes[i].used ? 1 : 0;
        e[1] = nodes[i].directory ? 1 : 0;
        for (int j = 0; j < FS_NAME_MAX; ++j) e[2 + j] = (uint8_t)nodes[i].name[j];
        fs_put32(e + 50, nodes[i].size);
    }

    for (int i = 0; i < (int)sizeof(dir); i += 512)
        if (!ata_write_sector(FS_DIR_LBA + (uint32_t)(i / 512), dir + i)) return 0;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        uint8_t data[512];
        for (int j = 0; j < 512; ++j) data[j] = 0;
        if (!nodes[i].used || nodes[i].directory) continue;
        for (uint32_t j = 0; j < nodes[i].size; ++j) data[j] = (uint8_t)nodes[i].data[j];
        if (!ata_write_sector(FS_DATA_LBA + (uint32_t)i, data)) return 0;
    }

    super[8] = super[9] = super[10] = super[11] = 0;
    fs_put32(super + 8, fs_checksum(super, 512));
    return ata_write_sector(FS_SUPER_LBA, super);
}

static void fs_seed_defaults(void) {
    fs_reset();
    fs_create_dir("/home");
    fs_create_dir("/desktop");
    fs_create_dir("/documents");
    fs_create_dir("/downloads");
    fs_create_file("/home/readme.txt");
    fs_write("/home/readme.txt", "welcome to myos\n", 16);
    fs_create_file("/desktop/welcome.txt");
    fs_write("/desktop/welcome.txt", "myos desktop\n", 13);
}

void fs_init(void) {
    fs_ready = 0;
    fs_persistent = 0;
    if (fs_load_disk()) {
        fs_ready = 1;
        fs_persistent = 1;
        return;
    }
    fs_seed_defaults();
    fs_ready = 1;
    if (fs_disk_geometry_ok()) {
        fs_persistent = 1;
        (void)fs_sync_disk();
    }
}

int fs_available(void) {
    return fs_ready != 0;
}

int fs_persistent_storage(void) {
    return fs_persistent != 0;
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
    if (!fs_ready || !fs_valid_path(path) || !fs_parent_exists(path) || fs_find(path) >= 0) return -1;
    for (int i = 1; i < FS_MAX_FILES; ++i) {
        if (nodes[i].used) continue;
        nodes[i].used = 1;
        nodes[i].directory = 0;
        nodes[i].size = 0;
        nodes[i].data[0] = 0;
        fs_copy(nodes[i].name, path, FS_NAME_MAX);
        (void)fs_sync_disk();
        return i;
    }
    return -1;
}

int fs_create_dir(const char* path) {
    if (!fs_ready || !fs_valid_path(path) || !fs_parent_exists(path) || fs_find(path) >= 0) return -1;
    for (int i = 1; i < FS_MAX_FILES; ++i) {
        if (nodes[i].used) continue;
        nodes[i].used = 1;
        nodes[i].directory = 1;
        nodes[i].size = 0;
        nodes[i].data[0] = 0;
        fs_copy(nodes[i].name, path, FS_NAME_MAX);
        (void)fs_sync_disk();
        return i;
    }
    return -1;
}

int fs_delete_index(int index) {
    if (!fs_ready || index <= 0 || index >= FS_MAX_FILES || !nodes[index].used) return 0;
    if (nodes[index].directory && !fs_dir_empty(index)) return 0;
    nodes[index].used = 0;
    nodes[index].directory = 0;
    nodes[index].size = 0;
    nodes[index].name[0] = 0;
    nodes[index].data[0] = 0;
    (void)fs_sync_disk();
    return 1;
}

int fs_delete(const char* path) {
    return fs_delete_index(fs_find(path));
}

int fs_rename(const char* old_path, const char* new_path) {
    int index = fs_find(old_path);
    if (index <= 0 || !fs_valid_path(new_path) || !fs_parent_exists(new_path) ||
        fs_find(new_path) >= 0) return 0;
    fs_copy(nodes[index].name, new_path, FS_NAME_MAX);
    (void)fs_sync_disk();
    return 1;
}

int fs_write(const char* path, const char* data, uint32_t len) {
    int index = fs_find(path);
    if (index <= 0 || nodes[index].directory || !data) return 0;
    if (len >= FS_DATA_MAX) len = FS_DATA_MAX - 1;
    for (uint32_t i = 0; i < len; ++i) nodes[index].data[i] = data[i];
    nodes[index].data[len] = 0;
    nodes[index].size = len;
    (void)fs_sync_disk();
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
