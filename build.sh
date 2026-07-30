#!/bin/bash
set -e

# ===============================================
# MYOS Build Script
# 32-bit x86 OS with GRUB Multiboot
# ===============================================

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_DIR"

# Compiler and linker
CC="${CC:-gcc}"
LD="${LD:-ld}"
AS="${AS:-as}"

# Compiler flags
CFLAGS="-m32 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -nostdinc -Wall -Wextra -Werror -I."
LDFLAGS="-m elf_i386 -T linker.ld"

# Assembly flags for GNU assembler (non-GCC path)
ASFLAGS="--32"

# Source files (C)
C_SOURCES="
    kernel.c
    idt.c
    cpu.c
    serial.c
    timer.c
    keyboard.c
    mouse.c
    paging.c
    pmm.c
    heap.c
    memory.c
    gfx.c
    desktop.c
    capability.c
    syscall.c
    initrd.c
    vfs.c
    user.c
    scheduler.c
    aegis.c
    ata.c
    mehtafs.c
    pci.c
    rtl8139.c
    aura_net.c
    telemetry.c
"

# Assembly files (GAS syntax .s)
S_SOURCES="
    boot.s
    interrupts.s
    syscall_entry.s
    task_switch.s
"

# Object files
OBJECTS=""

echo "=========================================="
echo " MYOS - Building x86 Kernel"
echo "=========================================="

# Compile C files
echo "--- Compiling C sources ---"
for src in $C_SOURCES; do
    obj="${src%.c}.o"
    echo "  CC  $src -> $obj"
    $CC $CFLAGS -c "$src" -o "$obj"
    OBJECTS="$OBJECTS $obj"
done

# Compile assembly files with GCC (preferred)
echo "--- Assembling sources ---"
for src in $S_SOURCES; do
    obj="${src%.s}.o"
    echo "  AS  $src -> $obj"
    $CC $CFLAGS -c "$src" -o "$obj"
    OBJECTS="$OBJECTS $obj"
done

# Link
echo "--- Linking ---"
echo "  LD  myos.bin"
$LD $LDFLAGS -o myos.bin $OBJECTS

echo "--- Checking Multiboot ---"
if grub-file --is-x86-multiboot myos.bin; then
    echo "  [OK] myos.bin is Multiboot compliant"
else
    echo "  [FAIL] myos.bin is NOT Multiboot compliant!"
    exit 1
fi

# Create ISO
echo "--- Creating ISO ---"
mkdir -p isodir/boot/grub
cp myos.bin isodir/boot/myos.bin
cp grub.cfg isodir/boot/grub/ 2>/dev/null || true
grub-mkrescue -o myos.iso isodir 2>/dev/null

echo "=========================================="
echo " Build complete: myos.bin"
echo " Run with: qemu-system-i386 -cdrom myos.iso"
echo "=========================================="
