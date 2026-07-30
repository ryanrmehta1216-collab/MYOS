#include <stdint.h>
#include <stddef.h>

extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

#define FS_FILE 0x01
#define FS_DIRECTORY 0x02

typedef struct vfs_node {
    char name[64];
    uint32_t flags;
    uint32_t length;
    uint8_t* data;
    struct vfs_node* next;
} vfs_node_t;

vfs_node_t* vfs_root = NULL;

void vfs_add_file(const char* name, const char* content, uint32_t length) {
    vfs_node_t* new_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!new_node) return;

    // Copy filename
    int i = 0;
    while (name[i] != '\0' && i < 63) {
        new_node->name[i] = name[i];
        i++;
    }
    new_node->name[i] = '\0';

    new_node->flags = FS_FILE;
    new_node->length = length;

    // Allocate memory block for file contents
    new_node->data = (uint8_t*)kmalloc(length + 1);
    for (uint32_t j = 0; j < length; j++) {
        new_node->data[j] = (uint8_t)content[j];
    }
    new_node->data[length] = '\0';

    new_node->next = NULL;

    // Append node to VFS linked list
    if (vfs_root == NULL) {
        vfs_root = new_node;
    } else {
        vfs_node_t* temp = vfs_root;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = new_node;
    }
}

vfs_node_t* vfs_find_file(const char* name) {
    vfs_node_t* curr = vfs_root;
    while (curr != NULL) {
        int match = 1;
        int i = 0;
        while (name[i] != '\0' || curr->name[i] != '\0') {
            if (name[i] != curr->name[i]) {
                match = 0;
                break;
            }
            i++;
        }
        if (match) return curr;
        curr = curr->next;
    }
    return NULL;
}