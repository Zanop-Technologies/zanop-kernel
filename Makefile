CXX = clang++
AS = nasm
LD = ld

CXXFLAGS = --target=x86_64-elf -ffreestanding -nostdlib \
           -fno-exceptions -fno-rtti -fno-stack-protector \
           -mcmodel=large -mno-red-zone -O2 -no-pie

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
       src/net/arp.cpp \
       src/fs/fs.cpp \
       src/memory/allocator.cpp \
       src/memory/paging.cpp \
       src/process.cpp \
       src/signals.cpp \
       src/syscall.cpp \
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
       ../zanop-desktop-os/compositor/src/window.cpp \
       src/installer_installer.cpp 
       

OBJS = $(SRCS:.cpp=.o)
OBJS += boot.o

all: kernel.bin

boot.o: src/arch/x86_64/boot.asm
	$(AS) -f elf64 $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

kernel.bin: $(OBJS) src/arch/x86_64/linker.ld
	$(LD) -T src/arch/x86_64/linker.ld -o $@ $(OBJS)

clean:
	rm -f $(OBJS) kernel.bin

run: kernel.bin
	qemu-system-x86_64 -kernel kernel.bin

.PHONY: all clean run