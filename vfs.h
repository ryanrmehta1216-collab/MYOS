#ifndef VFS_H
#define VFS_H

#include <stdint.h>

/* Virtual File System node types */
#define FS_FILE        0x01
#define FS_DIRECTORY   0x02

/* VFS node structure */
typedef struct vfs_node {
    char name[64];
    uint32_t flags;
    uint32_t length;
    uint8_t* data;
    struct vfs_node* next;
} vfs_node_t;

extern vfs_node_t* vfs_root;

/* Add a file to the VFS */
void vfs_add_file(const char* name, const char* content, uint32_t length);

/* Find a file in the VFS by name */
vfs_node_t* vfs_find_file(const char* name);

#endif /* VFS_H */
