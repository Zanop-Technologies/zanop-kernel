CXX = clang++
AS = nasm
LD = ld

CXXFLAGS = --target=x86_64-elf -ffreestanding -nostdlib \
           -fno-exceptions -fno-rtti -fno-stack-protector \
           -mcmodel=large -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -O2 -no-pie -Isrc

SRCS = src/main.cpp \
       src/drivers/vga.cpp \
       src/drivers/keyboard.cpp \
       src/drivers/mouse.cpp \
       src/drivers/ata.cpp \
       src/drivers/serial.cpp \
       src/drivers/e1000.cpp \
       src/arch/x86_64/pic.cpp \
       src/arch/x86_64/idt.cpp \
       src/arch/x86_64/mod.cpp \
       src/arch/x86_64/usermode.cpp \
       src/net/arp.cpp \
       src/fs/fs.cpp \
       src/memory/allocator.cpp \
       src/memory/paging.cpp \
       src/process.cpp \
       src/signals.cpp \
       src/syscall.cpp \
       src/posix/posix.cpp \
       src/posix/kernel_backend.cpp \
       src/posix/libc_shims.cpp \
       src/terminal.cpp \
       src/framebuffer/framebuffer.cpp \
       src/framebuffer/multiboot.cpp \
       src/panic.cpp \
       src/shell/shell.cpp \
        \
        \
       src/apps/apps.cpp \
       src/apps/clock.cpp \
       src/apps/calculator.cpp \
       src/apps/snake.cpp \
       src/apps/notes.cpp \
       src/auth/users_manager.cpp \
       src/compositor_surface.cpp \
       ../zanop-desktop-os/installer/src/installer.cpp \
       ../zanop-desktop-os/installer/src/vga_font.cpp \
       ../zanop-desktop-os/installer/src/gui.cpp \
       ../zanop-desktop-os/input/src/input.cpp \
       ../zanop-desktop-os/shell/src/mod.cpp \
       ../zanop-desktop-os/shell/src/desktop.cpp \
       ../zanop-desktop-os/shell/src/taskbar.cpp \
       ../zanop-desktop-os/shell/src/launcher.cpp \
       ../zanop-desktop-os/shell/src/apps_gui.cpp \
       ../zanop-desktop-os/shell/src/legacy_shell.cpp \
       ../zanop-desktop-os/compositor/src/window.cpp \
       src/installer_installer.cpp 
# DO NOT REMOVE      
MISC = build/misc/AUTHORS \
       build/misc/CHANGELOG \
       build/misc/LICENSE.txt \
       build/misc/README \
       build/misc/licenses/AGPL.txt \
       build/misc/licenses/CC-BY-SA.txt

# `make selftest` builds a kernel that runs the POSIX/ring-3 self-test at boot.
ifdef ZANOP_SELFTEST
CXXFLAGS += -DZANOP_SELFTEST
SRCS += src/posix/selftest.cpp
SELFTEST_OBJS = user_programs.o
endif

OBJS = $(SRCS:.cpp=.o)
OBJS += boot.o usermode_asm.o $(SELFTEST_OBJS)

all: kernel.bin

boot.o: src/arch/x86_64/boot.asm
	$(AS) -f elf64 $< -o $@

usermode_asm.o: src/arch/x86_64/usermode.asm
	$(AS) -f elf64 $< -o $@

user_programs.o: src/posix/user_programs.asm
	$(AS) -f elf64 $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

kernel.bin: $(OBJS) src/arch/x86_64/linker.ld
	$(LD) -T src/arch/x86_64/linker.ld -o $@ $(OBJS)

clean:
	rm -f $(OBJS) usermode_asm.o user_programs.o kernel.bin tests/posix/test_posix

zanop.iso: kernel.bin isodir/boot/grub/grub.cfg $(MISC)
	cp kernel.bin isodir/boot/kernel.bin
	cp -r build/misc/. isodir/
	md5sum isodir/boot/kernel.bin > isodir/KERNEL.md5
	grub-mkrescue -o zanop.iso isodir
	md5sum zanop.iso > zanop.iso.md5

run: zanop.iso
	qemu-system-x86_64 -cdrom zanop.iso

# Host-side unit tests for the portable POSIX core (no kernel, no QEMU).
HOSTCXX ?= g++

tests/posix/test_posix: tests/posix/test_posix.cpp src/posix/posix.cpp src/posix/posix.hpp src/posix/posix_types.hpp
	$(HOSTCXX) -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Isrc tests/posix/test_posix.cpp src/posix/posix.cpp -o $@

test-posix: tests/posix/test_posix
	./tests/posix/test_posix

# Boot self-test: kernel-mode syscalls against a real disk, then ring-3 programs.
# Needs QEMU and 1 GiB of RAM. Output appears on the serial console (this terminal).
# Run `make clean` afterwards to go back to a normal build.
SELFTEST_DISK ?= selftest-disk.img

selftest:
	$(MAKE) clean
	$(MAKE) ZANOP_SELFTEST=1 zanop.iso

run-selftest: selftest
	qemu-img create -f raw $(SELFTEST_DISK) 64M
	qemu-system-x86_64 -cdrom zanop.iso -drive file=$(SELFTEST_DISK),format=raw,if=ide \
		-boot d -m 1024 -serial stdio

.PHONY: all clean run test-posix selftest run-selftest
