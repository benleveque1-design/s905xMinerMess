CC ?= gcc
CFLAGS ?= -O3 -pthread -Wall -Wextra
CRYPTO_FLAGS = -march=armv8-a+crypto
TARGET = bitcoin_sha256d_s905x
SRC = bitcoin_sha256d_s905x.c

# Generic flags on every host: the ARMv8 HW-SHA functions opt into the Crypto
# Extension per-function via target("+crypto") and runtime HWCAP_SHA2 dispatch
# selects the backend, so one binary serves S905X (HW) and S905-class cores
# lacking the extension (SW fallback).
BUILD_FLAGS = $(CFLAGS)

.PHONY: all crypto generic test benchmark clean help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(BUILD_FLAGS) -o $@ $<

crypto: $(SRC)
	$(CC) $(CFLAGS) $(CRYPTO_FLAGS) -o $(TARGET) $<

generic: $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $<

test: $(TARGET)
	./$(TARGET) -t

benchmark: $(TARGET)
	./$(TARGET) -b -n 10000000

clean:
	rm -f $(TARGET) *.o

help:
	@echo "S905X Bitcoin SHA-256d Miner Makefile"
	@echo "Targets:"
	@echo "  make            - Build native binary (runtime-selects ARMv8 SHA backend when present)"
	@echo "  make crypto     - Force AArch64 ARMv8 Crypto build (-march=armv8-a+crypto)"
	@echo "  make generic    - Build generic portable binary (software fallback)"
	@echo "  make test       - Build and execute correctness test suite (-t)"
	@echo "  make benchmark  - Build and execute 10M-iteration benchmark (-b)"
	@echo "  make clean      - Remove compiled binary"
