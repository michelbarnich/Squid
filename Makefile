# Standalone command-line build for Squid.

CC = clang
CFLAGS = -Wall -Wextra -Wpedantic -std=c17 -arch arm64
LDFLAGS = -Wl,-dead_strip

# Source files for squidd
SQUIDD_SOURCES = \
    squidd/main.c \
    squidd/daemon/daemon.c \
    squidd/setup/setup.c \
    squidd/process/process_monitor.c \
    squidd/security/code_identity.c \
    squidd/security/target_task.c \
    squidd/macho_loader/mach_o_parser.c \
    squidd/macho_loader/remote_vm.c \
    squidd/macho_loader/relocation.c \
    squidd/macho_loader/mach_loader.c

SQUIDD_INCLUDES = -I squidd/macho_loader \
                 -I squidd/process \
                 -I squidd/security \
                 -I squidd/setup

SQUIDD_FRAMEWORKS = -framework CoreFoundation \
                   -framework Security

LIBSQUID_SOURCES = libsquid/main.c libsquid/swizzle.c

BUILDDIR = build
SQUIDD_BIN = $(BUILDDIR)/squidd
LIBSQUID_DYLIB = $(BUILDDIR)/liblibsquid.dylib

.PHONY: all clean squidd libsquid test

all: squidd libsquid
	@echo "Build complete!"

squidd: $(SQUIDD_BIN)

$(SQUIDD_BIN): $(SQUIDD_SOURCES)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(SQUIDD_INCLUDES) \
		-I /usr/include \
		$(SQUIDD_FRAMEWORKS) \
		-o $(SQUIDD_BIN) $^

libsquid: $(LIBSQUID_DYLIB)

$(LIBSQUID_DYLIB): $(LIBSQUID_SOURCES)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -dynamiclib $(LDFLAGS) -lobjc \
		-o $(LIBSQUID_DYLIB) $^

test: squidd libsquid
	@echo "Running smoke test..."
	@echo "Checking build products..."
	@file $(LIBSQUID_DYLIB) | grep -q "Mach-O 64-bit dynamically linked shared library arm64" && echo "✓ arm64 dylib built"
	@test -x $(SQUIDD_BIN) && echo "✓ Squid binary is executable"
	@echo "All tests passed!"

clean:
	rm -rf $(BUILDDIR)
	@echo "Clean complete!"
