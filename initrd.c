#include <stdint.h>
#include <stddef.h>

extern void vfs_add_file(const char* name, const char* content, uint32_t length);

void init_initrd() {
    const char* file1_name = "welcome.txt";
    const char* file1_data = "VFS INITIALIZED SUCCESSFULLY!";

    const char* file2_name = "sysinfo.txt";
    const char* file2_data = "LOOK OS v1.0 - 32-BIT x86 PROTECTED MODE KERNEL";

    uint32_t len1 = 0;
    while (file1_data[len1] != '\0') len1++;

    uint32_t len2 = 0;
    while (file2_data[len2] != '\0') len2++;

    // Register embedded files into the Kernel Virtual File System
    vfs_add_file(file1_name, file1_data, len1);
    vfs_add_file(file2_name, file2_data, len2);
}