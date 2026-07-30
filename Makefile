# =============================================
# MYOS v2.0 — Professional Build System
# 32-bit x86 Kernel with GRUB Multiboot
# 
# Targets:
#   all      — Build kernel + ISO
#   run      — Build + launch QEMU with RTL8139 + COM1 serial
#   run-serial — Launch with serial output to terminal
#   clean    — Remove all build artifacts
#   iso      — Build ISO only
#   objdump  — Dump kernel symbols
#   size     — Show kernel section sizes
# =============================================

# === Toolchain ===
CC      := gcc
LD      := ld
AS      := gcc
CFLAGS  := -m32 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -nostdinc -Wall -Wextra -Werror -Iinclude
LDFLAGS := -m elf_i386 -T linker.ld
ASFLAGS := -m32 -ffreestanding -c

# === Source files are in src/ directory; headers in include/ ===
VPATH := src

C_SOURCES := \
	kernel.c \
	idt.c cpu.c serial.c timer.c keyboard.c mouse.c \
	paging.c pmm.c heap.c memory.c gfx.c desktop.c \
	capability.c syscall.c initrd.c vfs.c user.c \
	scheduler.c aegis.c ata.c mehtafs.c pci.c \
	rtl8139.c aura_net.c telemetry.c \
	slab.c panic.c

S_SOURCES := \
	boot.s interrupts.s syscall_entry.s task_switch.s

# === Object derivation ===
C_OBJECTS := $(C_SOURCES:.c=.o)
S_OBJECTS := $(S_SOURCES:.s=.o)
OBJECTS   := $(C_OBJECTS) $(S_OBJECTS)
OBJ_DIR   := build

# === Targets ===
KERNEL_BIN := myos.bin
ISO_IMAGE  := myos.iso
ISO_DIR    := isodir

.PHONY: all run run-serial clean iso objdump size

all: $(ISO_IMAGE)

# === Compilation rules ===
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

%.o: %.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# === Link ===
$(KERNEL_BIN): $(OBJECTS)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "  [OK]  $@ linked successfully"

# === Multiboot check ===
multiboot_check: $(KERNEL_BIN)
	@if grub-file --is-x86-multiboot $(KERNEL_BIN); then \
		echo "  [OK]  $(KERNEL_BIN) is Multiboot compliant"; \
	else \
		echo "  [FAIL] $(KERNEL_BIN) is NOT Multiboot compliant!"; \
		exit 1; \
	fi

# === ISO creation ===
$(ISO_IMAGE): $(KERNEL_BIN) multiboot_check
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL_BIN) $(ISO_DIR)/boot/myos.bin
	@cp grub.cfg $(ISO_DIR)/boot/grub/ 2>/dev/null || true
	@grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR) 2>/dev/null
	@echo "  [OK]  ISO created: $(ISO_IMAGE)"

# === QEMU launch ===
QEMU_FLAGS := -cdrom $(ISO_IMAGE) -m 128

# Standard QEMU with RTL8139 + COM1 serial
qemu_flags = $(QEMU_FLAGS) \
	-netdev user,id=net0 \
	-device rtl8139,netdev=net0 \
	-serial stdio

run: $(ISO_IMAGE)
	@echo "  [RUN] Launching QEMU..."
	@qemu-system-i386 $(qemu_flags)

run-serial: $(ISO_IMAGE)
	@echo "  [RUN] Launching QEMU with serial console..."
	@qemu-system-i386 $(QEMU_FLAGS) \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-serial stdio

# Without networking (lighter)
run-minimal: $(ISO_IMAGE)
	@echo "  [RUN] Launching QEMU (minimal)..."
	@qemu-system-i386 $(QEMU_FLAGS)

# === Debugging ===
objdump: $(KERNEL_BIN)
	objdump -d $(KERNEL_BIN) | head -200

size: $(KERNEL_BIN)
	@echo "Kernel section sizes:"
	@size -A $(KERNEL_BIN)
	@echo ""
	@echo "Total kernel size:"
	@size $(KERNEL_BIN)

# === Clean ===
clean:
	@rm -f *.o *.d *.bin *.iso
	@rm -rf $(ISO_DIR)
	@echo "  [OK]  Clean completed"

# === Dependencies ===
%.d: %.c
	@$(CC) $(CFLAGS) -MM -MT $@ -MF $*.d $<

-include $(C_SOURCES:.c=.d)
