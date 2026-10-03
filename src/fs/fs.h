#ifndef MYOS_FS_H
#define MYOS_FS_H

#include <stdint.h>

#define FS_MAX_FILES 32
#define FS_NAME_MAX  48
#define FS_DATA_MAX  512

typedef struct {
    int index;
    char name[FS_NAME_MAX];
    uint32_t size;
    int directory;
} fs_entry_t;

void fs_init(void);
int fs_available(void);
int fs_count(void);
int fs_list(fs_entry_t* out, int max_entries);
int fs_create_file(const char* path);
int fs_create_dir(const char* path);
int fs_delete(const char* path);
int fs_delete_index(int index);
int fs_rename(const char* old_path, const char* new_path);
int fs_write(const char* path, const char* data, uint32_t len);
int fs_read(const char* path, char* out, uint32_t cap);
int fs_stat(const char* path, fs_entry_t* out);
const char* fs_data(int index);

#endif
