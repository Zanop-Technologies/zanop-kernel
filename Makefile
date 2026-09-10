CXX = clang++
AS = nasm
LD = ld.lld

CXXFLAGS = --target=x86_64-elf -ffreestanding -nostdlib \
           -fno-exceptions -fno-rtti -fno-stack-protector \
           -mcmodel=large -mno-red-zone -O2

SRCS = src/main.cpp \
       src/drivers/vga.cpp \
       src/drivers/keyboard.cpp \
       src/drivers/serial.cpp \
       src/arch/x86_64/pic.cpp \
       src/arch/x86_64/idt.cpp \
       src/fs/fs.cpp \
       src/shell/shell.cpp \
       src/apps/apps.cpp \
       src/apps/clock.cpp \
       src/apps/calculator.cpp \
       src/apps/snake.cpp \
       src/apps/notes.cpp
       

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