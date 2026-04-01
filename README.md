# Capstone Docker Lab

## Project Overview

This lab implements a 2-container offensive pipeline:

1. **c2-server** — Command-and-control listener and handler
2. **exfil-server** — Static payload hosting (exploit website + binary distribution)

### Behavior
- Implant uses environment variables (`C2_HOST`, `C2_PORT`) for runtime flexibility — no hardcoded IPs.
- In Docker, containers communicate by hostname (`c2-server`, `exfil-server`) on `labnet`.
- External VM targets use the host machine's IP (`<host-ip>:4444`, `<host-ip>:8000`, `<host-ip>:8888`).

---

## Project Structure

```
/ (workspace root)
├── docker-compose.yml          # Starts c2-server + exfil-server on custom network
├── README.md                   # This file
├── c2-server/
│   ├── Dockerfile              # Builds C2 image (Linux controller + Windows cross-compile)
│   └── C2-Server-and-Implant/
│       ├── src/                # C source: controller, implant, protocol, utils
│       ├── include/            # Headers: protocol, platform, utils
│       ├── Makefile            # Builds Linux + Windows binaries
│       └── bin/                # Build output
├── exfil-server/
│   ├── Dockerfile              # Multi-stage: compiles implant.exe, then builds file server
│   └── CVE-2021-21191---CVE-2021-21192/
│       ├── index.html          # CVE exploit webpage
│       ├── exploit.js          # Exploit script
│       └── server.js           # Node.js static file server (port 8888)
└── exfil-data/                 # Persisted volume for exfiltrated files
```

---

## What Each Container Does

### c2-server
- Docker image: `docker-c2-server`
- Build: installs `gcc`, `make`, `mingw-w64`; runs `make` to compile all binaries
- Runs: `./bin/linux/controller` — interactive terminal menu for controlling implants
- Exposed port: `4444`
- Environment:
  - `C2_HOST=0.0.0.0`
  - `C2_PORT=4444`

Controller reads `C2_PORT` from environment at startup to determine which port to bind.

### exfil-server
- Docker image: `docker-exfil-server`
- Build: **multi-stage**
  - Stage 1 (`builder`): compiles fresh `implant.exe` from source using `mingw-w64`
  - Stage 2: installs `nodejs`, `npm`, Python; copies compiled `implant.exe` and exploit site
- Runs:
  - Node.js exploit site on port `8888`
  - Python static file server on port `8000`
- Exposed ports: `8000`, `8888`

---

## Docker Compose Networking

`docker-compose.yml` defines:
- `labnet` bridge network (container DNS + routing)
- c2-server maps `4444:4444`
- exfil-server maps `8000:8000`, `8888:8888`

| Access point | URL |
|---|---|
| C2 listener (TCP) | `localhost:4444` |
| Exploit webpage | `localhost:8888` |
| File server (implant.exe download) | `localhost:8000` |

Container-to-container:
- `c2-server` from exfil: `c2-server:4444`
- `exfil-server` from c2: `exfil-server:8888` or `exfil-server:8000`

---

## Usage

**Before building, set your host IP in `.env`** — this bakes the IP into `implant.exe` at compile time so it connects back automatically when run on the Windows VM:

```bash
# Find your host IP
ip addr show | grep "inet " | grep -v 127.0.0.1

# Edit .env and fill in C2_HOST_IP
nano .env
# C2_HOST_IP=192.168.1.100  ← your actual IP
```

```bash
cd "/home/hero/Documents/CS 564/Docker"

# Remove old containers/images if needed
sudo docker compose down
sudo docker system prune -a --volumes

# Build and run
sudo docker compose up --build -d

# Check status
sudo docker compose ps
sudo docker compose logs -f c2-server exfil-server

# Attach to the C2 controller (interactive menu)
sudo docker attach c2-server
# To detach without stopping the container: Ctrl+P then Ctrl+Q

# Debug into containers
sudo docker exec -it c2-server /bin/bash
sudo docker exec -it exfil-server /bin/bash

# Stop everything
sudo docker compose down
```

---

## Windows VM Network Settings

Before starting the Windows VM, configure its network adapter so it can reach the Linux host. The recommended mode depends on your hypervisor.

### Recommended: Bridged Adapter

The VM gets its own IP on the same physical network as your Linux machine. The Linux host IP is directly reachable from the VM.

**VirtualBox:**
1. With the VM powered off, open **Settings → Network**
2. Set **Attached to** → `Bridged Adapter`
3. Set **Name** → your active physical NIC (e.g. `eth0`, `wlan0`)
4. Click OK, start the VM

**VMware Workstation:**
1. With the VM powered off, open **VM → Settings → Network Adapter**
2. Select `Bridged: Connected directly to the physical network`
3. Click OK, start the VM

After booting, the Windows VM will get a DHCP address on the same subnet as your Linux host. Verify connectivity:
```cmd
ping <linux-host-ip>
```

---

### Alternative: Host-Only (isolated lab, no internet on VM)

Use this if you want the VM isolated from your wider network. The VM can only reach the Linux host, not the internet.

**VirtualBox:**
1. Go to **File → Tools → Network Manager** and ensure a Host-Only network exists (e.g. `vboxnet0`, typically `192.168.56.0/24`)
2. VM **Settings → Network** → `Host-only Adapter` → select `vboxnet0`
3. Your Linux host appears to the VM as `192.168.56.1` — use this as your `C2_HOST_IP`

**VMware Workstation:**
1. VM **Settings → Network Adapter** → `Host-only: A private network shared with the host`
2. Your Linux host's VMnet1 IP is the `C2_HOST_IP` (check with `ip addr show vmnet1`)

---

### What NOT to use

| Mode | Why it won't work |
|------|-------------------|
| **NAT** | VM shares the host's IP — the Windows VM cannot reach `HOST_IP:8000` or `HOST_IP:4444` directly |
| **Internal Network** | VMs can only talk to each other, not the Linux host |

---

## Accessing from an External Windows VM

From a Windows VM, `localhost` refers to the VM itself. Use your **Linux host IP** instead.

**Find your Linux host IP:**
```bash
ip addr show | grep "inet " | grep -v 127.0.0.1
```

| What | URL |
|---|---|
| Exploit webpage | `http://<host-ip>:8888` |
| Download implant.exe | `http://<host-ip>:8000/implant.exe` |
| C2 listener | `<host-ip>:4444` (TCP) |

**If ports are blocked, open them on Linux:**
```bash
sudo ufw allow 4444/tcp
sudo ufw allow 8000/tcp
sudo ufw allow 8888/tcp
```

---

## Payload Flow

1. Windows VM browses to `http://<host-ip>:8888` (CVE exploit page).
2. Exploit fires — shellcode runs PowerShell, downloads `implant.exe` from `http://<host-ip>:8000/implant.exe` and executes it silently.
3. Implant automatically connects back to `<host-ip>:4444` — no user input needed, IP is baked in at build time.
4. C2 controller receives the connection and presents the interactive command menu.
5. Operator attaches via `sudo docker attach c2-server` and issues commands.

---

## C2 Controller Commands

Once an implant connects, `sudo docker attach c2-server` shows:

```
Select a command:
  1 - HEARTBEAT    Check implant is alive
  2 - SET_SLEEP    Make implant sleep N seconds, then reconnect
  3 - SHUTDOWN     Terminate implant
  4 - READ_DATA    Read a file from the target
  5 - WRITE_DATA   Write a file to the target
  6 - RUN_CMD      Execute a shell command on the target
```

---

## Bugs Fixed

### Bug 1 — Controller hardcoded port 8080 (c2-server/C2-Server-and-Implant/src/controller.c)

**Problem:** The controller source had `htons(8080)` hardcoded. The implant connects to port `4444` (via `C2_PORT` env var) and docker-compose only exposes `4444:4444`. The implant could never connect.

**Fix:** Controller now reads `C2_PORT` from the environment at startup, defaulting to `4444` if unset. The hardcoded `8080` was removed.

---

### Bug 2 — exfil-server served stale pre-built implant.exe

**Problem:** The exfil-server Dockerfile copied `bin/windows/implant.exe` directly from the host repo — a pre-built binary that predates the env var (`C2_HOST`/`C2_PORT`) support. The c2-server container compiled a fresh binary from source, but it stayed inside that container and was never shared. The Windows VM therefore downloaded an old binary that had `192.168.30.10` hardcoded and could not connect to the lab host.

**Fix:** Converted exfil-server to a **multi-stage Docker build**. Stage 1 (`builder`) compiles `implant.exe` fresh from source using `mingw-w64`. Stage 2 copies the compiled binary from Stage 1. No manual `make` on the host is ever required.

---

### Bug 4 — implant.exe had no baked-in C2 IP for autonomous execution

**Problem:** The implant fell back to the hostname `"c2-server"` when `C2_HOST` was not set. On an external Windows VM, that hostname does not resolve — the implant silently fails to connect. After an exploit fires the implant automatically, there is no opportunity to set env vars.

**Fix:** The host IP is now passed as a Docker build argument (`C2_HOST_IP`) through `docker-compose.yml` → `exfil-server/Dockerfile` → `make` → `-DC2_DEFAULT_HOST="<ip>"` into the compiler. The IP is baked into the binary at build time. Set `C2_HOST_IP` in the `.env` file before running `docker compose up --build`.

---

### Bug 3 — Python file server bound to localhost only

**Problem:** `python3 -m http.server` in Python 3.11 can default to binding `localhost` (127.0.0.1), making port 8000 unreachable from outside the container.

**Fix:** Added `--bind 0.0.0.0` to the CMD so the server explicitly listens on all interfaces.
