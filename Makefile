CC      ?= gcc
CFLAGS  := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-stack-protector -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
           -O2 -Wall -Wextra -Werror -nostdlib -Isrc
LDFLAGS := -m32 -T linker.ld -nostdlib -no-pie -Wl,--build-id=none -Wl,-z,noexecstack
OBJS    := build/boot.o build/kernel.o build/lib.o build/pmm.o build/paging.o build/kheap.o build/task.o build/demo_tasks.o build/selftest.o

all: build/kernel.elf

build/%.o: src/%.s | build
	$(CC) -m32 -c $< -o $@
build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/kernel.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
build:
	mkdir -p build

run: build/kernel.elf
	qemu-system-i386 -kernel build/kernel.elf -m 128

test-host: | build
	$(CC) -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc \
	  tests/host_test.c src/pmm.c src/kheap.c -o build/host_test
	./build/host_test

test-qemu: build/kernel.elf
	python3 tests/qemu_test.py

clean:
	rm -rf build
.PHONY: all run test-host test-qemu clean

