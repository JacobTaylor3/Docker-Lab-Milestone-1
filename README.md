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

> **Host dependencies:** `nasm` (shellcode assembler) and `mingw-w64` (Windows cross-compiler) must be installed on the host — `launch.sh` checks for both and will tell you if they are missing.

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

Once an implant connects, `sudo docker attach c2-server` shows the menu immediately. A status header appears above the menu on every prompt showing whether persistence is active:

```
+------------------------------------+
| C2 Controller                      |
| Persistence: ENABLED               |
+------------------------------------+
Select a command:
  1 - HEARTBEAT                  Check implant is alive
  2 - SET_SLEEP                  Make implant sleep N seconds, then reconnect
  3 - SHUTDOWN    (removes implant + task)
  4 - READ_DATA                  Read a file from the target
  5 - WRITE_DATA                 Write a file to the target
  6 - RUN_CMD                    Execute a shell command on the target
  7 - ENABLE PERSISTENCE         Create scheduled task on target
```

| Command | Description |
|---|---|
| HEARTBEAT | Sends a ping to the implant; expects `ALIVE` response. Confirms the connection is still live. |
| SET_SLEEP | Implant disconnects, sleeps N seconds, then reconnects. Controller waits at `accept()` for the reconnection. |
| SHUTDOWN | Implant deletes the scheduled task and its own executable (`C:\Users\Public\i.exe`), sends a confirmation, then exits. Controller exits cleanly. |
| READ_DATA | Reads a file at a given path on the target and returns its contents. |
| WRITE_DATA | Writes arbitrary data to a file at a given path on the target. |
| RUN_CMD | Runs a shell command on the target via `popen` and returns stdout. |
| ENABLE PERSISTENCE | Creates a scheduled task (`MicrosoftEdgeUpdate`) that runs the implant as SYSTEM on every user login. Updates the persistence status header. |

### Persistence Status Header

The controller auto-detects persistence immediately after the implant sends its HELLO packet by silently running a `schtasks /query` check. The result is shown in the header on every prompt:

| Status | Meaning |
|---|---|
| `UNKNOWN` | Auto-detection failed (connection issue during check) |
| `DISABLED` | Scheduled task does not exist on the target |
| `ENABLED` | Scheduled task exists — implant will survive reboots |

### Implant Reconnect Behavior

The implant retries the C2 connection every 30 seconds if it fails — both on initial startup and after waking from `SET_SLEEP`. This means:
- If the C2 server is not yet up when the machine boots, the implant keeps retrying until it connects
- The controller loops back to `accept()` automatically when a connection drops, so no container restart is needed between sessions
