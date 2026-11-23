Q2_CC ?= clang
Q2_LD ?= ld.lld
FLAGS = --target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -mno-abicalls -fno-pic -G0 -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
CFLAGS = $(FLAGS) -Oz -Wall -Wextra -Werror -std=c11 -Isrc
OBJ = build/q2_platform.o build/q2_input.o build/cart_browser.o build/cart_browser_probe.o
all: dist/q2-femto8-browser-preview.zip
build/%.o: src/%.c src/q2_platform.h src/q2_input.h
	mkdir -p build
	$(Q2_CC) $(CFLAGS) -c $< -o $@
dist/.pico8/q2-pico8: $(OBJ)
	mkdir -p dist/.pico8
	$(Q2_LD) -m elf32ltsmip -static -e __start -s -z noexecstack -o $@ $(OBJ)
dist/.pico8/q2app: q2app
	mkdir -p dist/.pico8
	cp q2app $@
dist/q2-femto8-browser-preview.zip: dist/.pico8/q2-pico8 dist/.pico8/q2app dist/.pico8/carts/.keep
	cd dist && zip -q -FS q2-femto8-browser-preview.zip .pico8/q2-pico8 .pico8/q2app .pico8/carts/.keep
dist/.pico8/carts/.keep:
	mkdir -p dist/.pico8/carts
	: > $@
check: all
	$(CC) -O2 -Wall -Wextra -Werror -std=c11 -Isrc tests/browser_test.c src/cart_browser.c -o build/browser-test
	./build/browser-test
	file dist/.pico8/q2-pico8 | grep 'ELF 32-bit LSB'
	unzip -t dist/q2-femto8-browser-preview.zip
clean:
	rm -rf build dist
.PHONY: all check clean
