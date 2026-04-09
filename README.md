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
├── launch.sh                   # One-command setup: prompts for IP, builds + starts lab
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
│   ├── Dockerfile              # Multi-stage (Debian builder): patches shellcode into exploit.js, compiles implant.exe, builds file server
│   ├── shellcode-generation/
│   │   ├── src/stager.asm      # Flat x64 NASM shellcode — PEB walk → WinExec(powershell ...) — runs in-place on RWX WASM page
│   │   ├── bin/                # Build output: stager_patched.asm, final_shellcode.bin (pre-built by launch.sh)
│   │   └── Makefile            # Substitutes HOST_IP into stager.asm, assembles with nasm; accepts HOST_IP=
│   ├── exploit-contents/
│   │   ├── index.html          # CVE exploit webpage
│   │   ├── exploit.js          # Exploit script (shellcode array patched at Docker build time)
│   │   └── server.js           # Node.js static file server (port 8888)
│   └── web-server/             # Stealth HTTPS file server (nginx header masking, TLS)
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
- Build: **multi-stage** using Debian as the builder
  - Stage 1 (`builder`):
    1. Copies pre-built `final_shellcode.bin` (assembled by `launch.sh` before Docker runs)
    2. Patches the shellcode uint32 array into `exploit.js`
    3. Compiles `implant.exe` from source with C2 host IP baked in
  - Stage 2: installs `nodejs`, `npm`, Python; copies compiled `implant.exe` and patched exploit site
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

## Prerequisites

### Docker DNS (required on Ubuntu with systemd-resolved)

Ubuntu uses `127.0.0.53` as its DNS resolver, which Docker containers cannot reach (it's a loopback address). Without this fix, `apt-get` inside containers fails with exit code 100.

```bash
echo '{"dns": ["8.8.8.8", "8.8.4.4"]}' | sudo tee /etc/docker/daemon.json
sudo systemctl restart docker
```

Verify it works:
```bash
sudo docker run --rm debian:bookworm-slim apt-get update
```

> **No other host dependencies required.** `msfvenom` and all build tools run inside Docker — nothing needs to be installed on the host machine beyond Docker itself.

---

## Usage

Everything is handled by a single script. Run it and follow the prompts:

```bash
cd "/home/hero/Documents/CS 564/Docker"
sudo ./launch.sh
```

`launch.sh` will:
1. Check Docker and Docker Compose are installed
2. Detect available network interfaces — highlights the `vboxnet0` host-only adapter IP
3. Prompt for your host IP — auto-fills with `vboxnet0` if detected, creates `.env` automatically
4. Stop any existing lab containers
5. Check for port conflicts on 4444, 8000, 8888
6. Build shellcode locally: `make HOST_IP=<ip>` in `shellcode-generation/` assembles `final_shellcode.bin` from `stager.asm`
7. Run `docker compose up --build -d` — Docker copies the pre-built shellcode and patches it into `exploit.js`
7. Verify both containers are running and print a summary

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

# Stop everything, restart cleanly, and re-attach to C2
sudo docker compose down
sudo docker compose up -d
sudo docker attach c2-server

# Stop everything
sudo docker compose down

# Regenerate shellcode with a new IP (updates .env and rebuilds exfil-server only)
sudo ./launch.sh
```

---

## Windows VM Network Settings

Before starting the Windows VM, configure its network adapter so it can reach the Linux host.

### Required: Host-Only Adapter

Use a Host-Only adapter. This gives the VM a private network shared with the Linux host, which is reliable across all hardware setups. `launch.sh` will automatically detect and recommend this IP.

**VirtualBox:**
1. With the VM powered off, open **Settings → Network**
2. Set **Attached to** → `Host-only Adapter`
3. Set **Name** → `vboxnet0` (create it first if it doesn't exist: **File → Tools → Network Manager**)
4. Click OK, start the VM
5. Find the host-only IP on Linux: `ip addr show vboxnet0` — this is your `C2_HOST_IP`

**VMware Workstation:**
1. With the VM powered off, open **VM → Settings → Network Adapter**
2. Select `Host-only: A private network shared with the host`
3. Click OK, start the VM
4. Find the host-only IP on Linux: `ip addr show vmnet1` — this is your `C2_HOST_IP`

After booting, verify the VM can reach the host:
```cmd
ping <host-only-ip>
```

### What NOT to use

| Mode | Why it won't work |
|------|-------------------|
| **Bridged** | Depends on physical NIC and network — unreliable in many environments |
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
2. Exploit fires — shellcode runs PowerShell, downloads `implant.exe` from `http://<host-ip>:8000/implant.exe` silently.
3. PowerShell performs a fodhelper UAC bypass to run `implant.exe` as Administrator (no UAC prompt shown).
4. Implant automatically connects back to `<host-ip>:4444` — no user input needed, IP is baked in at build time.
5. C2 controller receives the connection and presents the interactive command menu.
6. Operator attaches via `sudo docker attach c2-server` and issues commands.

## Privilege Escalation — fodhelper UAC Bypass

The implant runs as Administrator without triggering a UAC prompt, provided the victim user is a member of the local Administrators group (standard on personal/lab Windows installs).

`fodhelper.exe` is a Microsoft-signed binary on Windows' auto-elevation whitelist. Before launching, it reads:

```
HKCU\Software\Classes\ms-settings\shell\open\command
```

If that key exists with a `DelegateExecute` value (even empty), Windows auto-elevates and runs the key's default value — no UAC prompt. Because the key is in `HKCU`, any user can write it without elevated rights.

The PowerShell command embedded in the stager:
1. Downloads `implant.exe` to `C:\Users\Public\i.exe`
2. Writes the registry key pointing to `i.exe`
3. Starts `fodhelper.exe` — it auto-elevates and runs `i.exe` as Administrator
4. Sleeps 3 seconds, then removes the registry key (cleanup)

### Verifying elevation on the target

Once the implant connects, run via `RUN_CMD`:

```
whoami /groups
```

Look for `Mandatory Label\High Mandatory Level` — confirms Administrator. Alternatively:

```
net session
```

Returns session info → Administrator. Returns `Access is denied` → not elevated.

---

## C2 Controller Commands

Once an implant connects, `sudo docker attach c2-server` shows the menu immediately:

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

---

### Bug 5 — msfvenom shellcode had incorrect Windows path escaping

**File:** `exfil-server/Dockerfile`

**Problem:** The original Dockerfile used `C:\\\\Windows\\\\Temp` in the msfvenom `CMD` parameter. After shell processing inside the Docker `RUN` instruction, this became `C:\\Windows\\Temp` (double backslash). PowerShell single-quoted strings treat backslashes as literals, so `\\` was interpreted as two backslashes — an invalid path — causing the shellcode's download step to silently fail.

**Fix:** Changed to `C:\\Windows\\Temp` in the Dockerfile (two backslashes), which shell-processes to `C:\Windows\Temp` (one backslash) — the correct Windows path.

---

### Bug 6 — msfvenom ran on the host, failing on unsupported OS

**File:** `exfil-server/Dockerfile`, `launch.sh`, `generate_shellcode.sh`

**Problem:** Shellcode generation required `msfvenom` to be installed on the host. The Metasploit apt repo does not support Ubuntu 24.04 (Noble) and Rapid7's installer does not support Debian bookworm, making setup fragile and host-dependent.

**Fix (initial):** Moved `msfvenom` inside Docker using `kalilinux/kali-rolling` as the builder base. Shellcode was generated and `exploit.js` patched automatically during `docker compose up --build`.

---

### Bug 8 — Donut shellcode replaced with flat NASM stager

**Files:** `exfil-server/shellcode-generation/src/stager.asm`, `exfil-server/shellcode-generation/Makefile`, `exfil-server/Dockerfile`

**Problem:** Donut-generated shellcode (and the custom `loader.c` → Donut pipeline before it) requires `VirtualAlloc(PAGE_EXECUTE_READWRITE)` at runtime to unpack a PE into memory. Chrome's renderer sandbox blocks all `VirtualAlloc` calls with executable flags — the allocation returns `NULL` and execution faults immediately with `STATUS_ACCESS_VIOLATION`. This applies to any Donut-wrapped binary, including a Donut-wrapped injector that tries to inject into another process: Donut itself needs executable memory before it can do anything.

**Fix:** Replaced the entire Donut pipeline with a single flat x64 NASM shellcode (`stager.asm`, ~300 bytes). It runs entirely in-place on the RWX WASM page the exploit already owns:
1. Walks the PEB (`GS:[0x60]`) to find `kernel32.dll` base without any imports
2. Parses the PE export table to resolve `WinExec`
3. Calls `WinExec` with an embedded PowerShell command that downloads and runs `implant.exe`

PowerShell runs as an unsandboxed process and handles the download — no new executable memory needed inside the renderer. The stager is pre-assembled locally by `launch.sh` (`make HOST_IP=<ip>`) before Docker builds, so the Dockerfile simply copies `final_shellcode.bin` and patches it into `exploit.js`.

---

### Bug 7 — C2 controller prompt not visible after `docker attach`

**Files:** `c2-server/C2-Server-and-Implant/src/controller.c`, `docker-compose.yml`

**Problem 1:** The controller used `printf("> ")` without a trailing newline and no `fflush(stdout)`. C's stdio only auto-flushes on a newline, so the prompt sat in the output buffer and was never displayed — leaving the terminal blank after attaching.

**Problem 2:** The container was started without `tty: true` or `stdin_open: true`, so `docker attach` had no proper TTY to connect to, causing `fgets` to immediately return EOF and spam "Input error. Try again."

**Fix:** Added `fflush(stdout)` after the prompt in `display_prompt()`. Added `tty: true` and `stdin_open: true` to the c2-server service in `docker-compose.yml`. The menu now appears immediately when an implant connects.
