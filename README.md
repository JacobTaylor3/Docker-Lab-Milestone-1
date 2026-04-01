# Capstone Docker Lab

## Project Overview

This lab implements a 2-container offensive pipeline:

1. **c2-server** — Command-and-control listener and handler
2. **exfil-server** — Static payload hosting (exploit website + binary distribution)

### Updated behavior (no hardcoded 192.168.30.0/24)
- Original project used static target IP `192.168.30.10` in implant.
- Updated project uses environment values (`C2_HOST`, `C2_PORT`) for runtime flexibility.
- In Docker, containers communicate by hostnames (`c2-server`, `exfil-server`) on `labnet`.
- External VM targets use the host machine's IP (`<host-ip>:4444`, `<host-ip>:8000`, `<host-ip>:8888`).

## Project structure

```
/ (workspace root)
├── docker-compose.yml          # Starts c2-server + exfil-server on custom network
├── README.md                   # This docs file
├── c2-server/
│   ├── Dockerfile              # Builds C2 image (Linux controller + Windows binary cross-compile)
│   └── C2-Server-and-Implant/   # source tree from repository
│       ├── src/                # C code for controller/implant/protocol
│       ├── include/            # protocol/platform headers
│       ├── Makefile            # builds Linux and Windows binaries
│       └── bin/                # build output (local/host after make)
├── exfil-server/
│   ├── Dockerfile              # Builds static file server + node exploit site
│   └── CVE-2021-21191.../      # exploit web app code (node server)
└── exfil-data/                 # persist volume for exfiltrated files
```

## What each container does

### c2-server
- Docker image: `docker-c2-server`
- Build:
  - installs build tools + `mingw-w64` for cross-compiling Windows payload
  - `make` builds:
    - `bin/linux/controller` (listener)
    - `bin/linux/implant` (Linux implant)
    - `bin/windows/implant.exe` (Windows implant)
- Runs: `./bin/linux/controller`
- Exposed port: `4444`
- Environment:
  - `C2_HOST=0.0.0.0`
  - `C2_PORT=4444`

Implant code uses environment variables:
- `C2_HOST` defaults to `c2-server` (Docker internal)
- `C2_PORT` defaults to `4444`

### exfil-server
- Docker image: `docker-exfil-server`
- Build:
  - installs `nodejs`, `npm`, plus Python runtime
  - copies `bin/windows/implant.exe` from c2 source path
  - copies CVE exploit site code into `/opt/exploit/CVE-2021-21191...`
- Runs:
  - node exploit site (port `8888`)
  - python static file server (port `8000`)
- Exposed ports: `8000`, `8888`

This server acts as the hosting point for `implant.exe` and exploit webpage.

## Docker compose networking

`docker-compose.yml` defines:
- `labnet` bridge network (container DNS + routing)
- c2-server maps `4444:4444`
- exfil-server maps `8000:8000`, `8888:8888`

Container-to-container addresses:
- `c2-server` from exfil: `c2-server:4444`
- `exfil-server` from c2: `exfil-server:8888` or `exfil-server:8000`

Host-to-container:
- C2: `localhost:4444`
- Exploit UI: `localhost:8888`
- Artifact host: `localhost:8000`

External VM target (Windows) use your host network IP:
- `HOST_IP:4444`, `HOST_IP:8888`, `HOST_IP:8000`

## Usage

```bash
cd /home/hero/Documents/CS\ 564/Docker

# remove old containers/images if needed
sudo docker compose down
sudo docker system prune -a --volumes

# build and run
sudo docker compose up --build -d

# check status
sudo docker compose ps
sudo docker compose logs -f c2-server exfil-server

# debug into containers
sudo docker exec -it c2-server /bin/bash
sudo docker exec -it exfil-server /bin/bash

# stop everything
sudo docker compose down
```

## How the payload flow works

1. exfil site hosts exploitable webpage page at `http://<host-ip>:8888` and raw implant.exe at `http://<host-ip>:8000/implant.exe`.
2. Windows target downloads implant from `http://<host-ip>:8000/implant.exe` .
3. Target executes implant; in Windows target, payload reads `C2_HOST`/`C2_PORT` (set via env/command args).
4. Payload connects back to C2 listener `http://<host-ip>:4444`/TCP `4444`.
5. `c2-server` receives and manages command-and-control traffic.

## Notes

- The old hardcoded value `192.168.30.10` is replaced by env-based config.
- Use `C2_HOST=c2-server` inside container, or `C2_HOST=<host-ip>` for external VM.
- `docker-compose` network and host mapping gives flexible topology.

