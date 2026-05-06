# Capstone Docker Lab

## Setup

Threat Model Diagram: https://drive.google.com/file/d/10E8f0o-9Dk7D49TNTwUJC52Zn2m6lrR1/view?usp=sharing

1. Clone the repo:
   ```bash
   git clone https://github.com/JacobTaylor3/Docker-Lab-Milestone-2.git
   cd Docker-Lab-Milestone-2
   ```

2. Download the Windows victim VM from the shared drive and import it into VirtualBox. In VirtualBox: **File → Tools → Network Manager** — note the IPv4 address of your host-only adapter (It needs to be in the `192.168.56.0/24` subnet). Whatever ip Address is assinged (we recommend 192.168.56.1) is your real C2 machine IP.

4. Start Docker Desktop (or the Docker daemon on Linux).

   > **Windows (WSL) users:** `launch.sh` uses `netsh` to add IP aliases to your VirtualBox adapter, which requires administrator rights. Right-click your WSL distro (Ubuntu or whichever you use) in the Start menu and select **Run as administrator** before continuing.

5. Run the launch script:
   ```bash
   sudo ./launch.sh
   ```
   When prompted for a **c2-redirector IP**, enter a different address on the same subnet (e.g. `192.168.56.10`). `launch.sh` adds it as an IP alias on `vboxnet0` so Docker can bind to it. The victim connects only to this alias — your real machine IP stays hidden.

   The script will:
   - Generate random tokens (`ENROLLMENT_TOKEN`, `DOWNLOAD_TOKEN`)
   - Generate the full PKI (CA, controller cert, nginx cert)
   - Build and start all five containers (~5 min first run due to OpenSSL cross-compile)

6. Once the lab is up, attach to the C2 controller:
   ```bash
   sudo docker attach c2-server
   ```
   Press Enter if the prompt does not appear immediately. Detach without stopping: **Ctrl+P then Ctrl+Q**.

7. Boot the Windows VM. The password is `victim`. Open a browser and navigate to:
   ```
   http://<c2-redirector-IP>:8888
   ```
   The CVE-2021-21220 exploit fires automatically. The shellcode downloads `MicrosoftEdgeUpdate.exe` from `https://<c2-redirector-IP>:8443/update/<token>`, runs it via a fodhelper UAC bypass, and the implant connects back to the C2 controller.

8. The implant session appears in the controller menu. Select it by number to enter the command loop.

---

## Infrastructure Overview

Six containers, each representing a distinct machine:

| Machine | Container | Port | Role |
|---------|-----------|------|------|
| 1 | `c2-redirector` | `:443` | socat TCP relay — victim-facing hop, hides real C2 IP |
| 2 | `c2-server` | (backnet only) | mTLS C2 listener — operator command interface |
| 3 | `delivery-server` | `:8443` | nginx HTTPS — single-use token implant download |
| 4 | `exploit-server` | `:8888` | CVE-2021-21220 exploit webpage |
| 5 | `exfil-redirector` | `:9443` | socat TCP relay — victim-facing hop, hides real exfil IP |
| 6 | `exfil-receiver` | (exfilnet only) | HTTPS POST sink — saves screenshots, keylogs, creds |

Two traffic channels, each with a redirector hop on a separate IP:
- **C2 channel** (`:443` mTLS): victim → `c2-redirector` (`C2_HOST_IP`) → `c2-server` (backnet only)
- **Exfil channel** (`:9443` HTTPS): victim → `exfil-redirector` (`EXFIL_HOST_IP`) → `exfil-receiver` (exfilnet only)

Exfil data is saved to `exfil-data/<hostname>/` on the host machine.

---

## Useful Commands

```bash
# Tail logs
sudo docker logs -f delivery-server
sudo docker logs -f exploit-server
sudo docker logs -f exfil-receiver

# Shell into a container
sudo docker exec -it c2-server /bin/bash

# Stop everything
sudo docker compose down

# View captured exfil data
ls exfil-data/
```

---

## Full Documentation

See [`documentation/ProjectOverview.md`](documentation/ProjectOverview.md) for the complete architecture, token/crypto pipeline, payload flow, and changelog.
