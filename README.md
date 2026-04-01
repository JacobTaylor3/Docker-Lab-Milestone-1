# Capstone Docker Lab

## Exploit Plan

This vulnerability is based on a website being hosted. The whole idea of it is that the victim goes to this website, does nothing, and then the exploit initiates via the shellcode in the exploit.js file (the js runs client-side –aka within the victim’s browser), and then the shellcode runs on the victim's local machine (within a form of their terminal). The shellcode (loader) then calls our python web server (hosting the implant.exe), downloads implant.exe, and runs it (all taking place within the victim's local machine). Once the implant is executed (which is on the victim's local machine), the implant executable (source written in C) then connects to our C2 server (controller.exe --also written in C) and then the C2 receives a message that a connection was made by the victim. Once the controller receives this message, the controller is able to have a reverse-shell in their terminal. This is done by allowing the controller to run windows command prompt commands (e.g. dir) remotely on the victim's machine from the controller's input. Implant.exe handles the subprocess runs, which are sent over the same ip/port by controller.exe user input.

## Project Overview

This lab implements a 2-container offensive pipeline:

1. **c2-server** — Command-and-control listener and handler
2. **exfil-server** — Static payload hosting (exploit website + binary distribution)

### Behavior
- The host IP is baked into `implant.exe` at build time — the implant connects back automatically with no user interaction after execution.
- In Docker, containers communicate by hostname (`c2-server`, `exfil-server`) on `labnet`.
- External VM targets use the host machine's IP (`<host-ip>:4444`, `<host-ip>:8000`, `<host-ip>:8888`).

### CVE Utilized

1. **CVE-2021-21220**: https://www.cvedetails.com/cve/CVE-2021-21220/

---

## Project Structure

```
/ (workspace root)
├── docker-compose.yml          # Starts c2-server + exfil-server on custom network
├── launch.sh                   # One-command setup: prompts for IP, generates shellcode, builds + starts lab
├── generate_shellcode.sh       # Standalone: regenerates shellcode + patches exploit.js for a given IP
├── .gitignore                  # Excludes .env, compiled binaries, shellcode artifacts
├── README.md                   # This file
├── c2-server/
│   ├── Dockerfile              # Builds C2 image (Linux controller + Windows cross-compile)
│   └── C2-Server-and-Implant/
│       ├── src/                # C source: controller, implant, protocol, utils
│       ├── include/            # Headers: protocol, platform, utils
│       ├── Makefile            # Builds Linux + Windows binaries; accepts C2_HOST_IP= to bake IP in
│       └── bin/                # Build output (gitignored — rebuilt by Docker)
├── exfil-server/
│   ├── Dockerfile              # Multi-stage: compiles implant.exe from source, then builds file server
│   └── CVE-2021-21191---CVE-2021-21192/
│       ├── index.html          # CVE exploit webpage
│       ├── exploit.js          # Exploit script (shellcode array patched by launch.sh)
│       └── server.js           # Node.js static file server (port 8888)
└── exfil-data/                 # Persisted volume for exfiltrated files (gitignored)
```

> `.env` is **not committed** — it is created automatically by `launch.sh` when you first run it.

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

Controller reads `C2_PORT` from the environment at startup to determine which port to bind.

### exfil-server
- Docker image: `docker-exfil-server`
- Build: **multi-stage**
  - Stage 1 (`builder`): compiles fresh `implant.exe` from source using `mingw-w64`, with host IP baked in via `-DC2_DEFAULT_HOST`
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

Everything is handled by a single script. Run it and follow the prompts:

```bash
cd "/home/hero/Documents/CS 564/Docker"
sudo ./launch.sh
```

`launch.sh` will:
1. Check Docker and Docker Compose are installed
2. Detect available network interfaces and IPs
3. Prompt for your host IP — creates `.env` automatically (first run) or updates it
4. Run `msfvenom` to generate shellcode with that IP and patch `exploit.js`
5. Stop any existing lab containers
6. Check for port conflicts on 4444, 8000, 8888
7. Run `docker compose up --build -d`
8. Verify both containers are running and print a summary

### Other useful commands

```bash
# Check container status
sudo docker compose ps

# Tail logs from both containers
sudo docker compose logs -f c2-server exfil-server

# Attach to the C2 controller (interactive menu)
sudo docker attach c2-server
# Detach without stopping: Ctrl+P then Ctrl+Q

# Shell into a container for debugging
sudo docker exec -it c2-server /bin/bash
sudo docker exec -it exfil-server /bin/bash

# Stop everything
sudo docker compose down

# Regenerate shellcode only (e.g. if IP changed)
sudo ./generate_shellcode.sh
```

---

## Windows VM Network Settings

Before starting the Windows VM, configure its network adapter so it can reach the Linux host.

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

After booting, verify the VM can reach the host:
```cmd
ping <linux-host-ip>
```

### Alternative: Host-Only (isolated lab, no internet on VM)

**VirtualBox:**
1. Go to **File → Tools → Network Manager** and ensure a Host-Only network exists (e.g. `vboxnet0`, typically `192.168.56.0/24`)
2. VM **Settings → Network** → `Host-only Adapter` → select `vboxnet0`
3. Your Linux host appears to the VM as `192.168.56.1` — use this as your `C2_HOST_IP`

**VMware Workstation:**
1. VM **Settings → Network Adapter** → `Host-only: A private network shared with the host`
2. Your Linux host's VMnet1 IP is the `C2_HOST_IP` (check with `ip addr show vmnet1`)

### What NOT to use

| Mode | Why it won't work |
|------|-------------------|
| **NAT** | VM shares the host's IP — cannot reach `HOST_IP:8000` or `HOST_IP:4444` directly |
| **Internal Network** | VMs can only talk to each other, not the Linux host |

---

## Accessing from the Windows VM

From a Windows VM, `localhost` refers to the VM itself. Use your **Linux host IP** instead.

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

### Bug 1 — Controller hardcoded port 8080

**File:** `c2-server/C2-Server-and-Implant/src/controller.c`

**Problem:** The controller had `htons(8080)` hardcoded. The implant connects to port `4444` (via `C2_PORT` env var) and docker-compose only exposes `4444:4444`. The implant could never connect.

**Fix:** Controller now reads `C2_PORT` from the environment at startup, defaulting to `4444` if unset.

---

### Bug 2 — exfil-server served stale pre-built implant.exe

**File:** `exfil-server/Dockerfile`

**Problem:** The Dockerfile copied `bin/windows/implant.exe` from the host repo — a pre-built binary with `192.168.30.10` hardcoded. The c2-server container compiled a fresh binary but it stayed inside that container and was never shared with exfil-server.

**Fix:** Converted to a multi-stage Docker build. Stage 1 (`builder`) compiles `implant.exe` fresh from source. Stage 2 copies it from Stage 1. The host repo binary is no longer used or tracked in git.

---

### Bug 3 — Python file server bound to localhost only

**File:** `exfil-server/Dockerfile`

**Problem:** `python3 -m http.server` can default to binding `127.0.0.1` in Python 3.11, making port 8000 unreachable from outside the container.

**Fix:** Added `--bind 0.0.0.0` so the server listens on all interfaces.

---

### Bug 4 — implant.exe had no baked-in C2 IP for autonomous execution

**Files:** `src/implant.c`, `Makefile`, `exfil-server/Dockerfile`, `docker-compose.yml`

**Problem:** The implant fell back to the hostname `"c2-server"` when `C2_HOST` was not set. On an external Windows VM that hostname does not resolve. After an exploit fires the implant automatically, there is no opportunity to set env vars.

**Fix:** The host IP flows from `.env` → `docker-compose.yml` build arg → `exfil-server/Dockerfile` ARG → `make C2_HOST_IP=<ip>` → `-DC2_DEFAULT_HOST="<ip>"` compiler flag → baked into `implant.exe`. `launch.sh` handles this automatically at startup.
