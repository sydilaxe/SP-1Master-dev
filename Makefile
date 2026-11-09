# SP-1 Master
#   make test       the mastering engine on the host: checks, and WAVs in build/test
#   make firmware   sp1master.bin (needs ZEPHYR_BASE and the Zephyr SDK; see README)
#   make bench      Cortex-M4 cycle estimate for the engine (needs the SDK and unicorn)
CC ?= cc
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -fno-math-errno -ffp-contract=off

test: build/master_test
	mkdir -p build/test
	./build/master_test build/test

build/master_test: test/master_test.c master/master.c master/master.h master/fx_math.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/master_test.c master/master.c -lm

firmware:
	./scripts/build-firmware.sh

bench:
	python3 tools/bench_m4.py -v

.PHONY: test firmware bench
