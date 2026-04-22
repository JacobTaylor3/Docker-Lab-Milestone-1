# Docker Lab Improvements and Changes

This document summarizes the changes made to the project to improve security, stealth, and build portability.

## 1. File Rename: `exploit.js` to `utils.js`
To reduce the footprint and avoid immediate detection by simple string-based scanners, the primary exploit script has been renamed.
- **Renamed:** `exfil-server/exploit-contents/exploit.js` → `exfil-server/exploit-contents/utils.js`
- **Updated References:** All references in the following files were updated to point to `utils.js`:
    - `exfil-server/exploit-contents/index.html`
    - `exfil-server/Dockerfile`
    - `exfil-server/exploit-contents/README.md`
    - Root `README.md`

## 2. Disabling Debug Symbols
To ensure smaller, more efficient, and harder-to-reverse binaries, debug symbols were removed from all compiled executables.
- **Modified:** `c2-server/C2-Server-and-Implant/Makefile`
- **Changes:**
    - Removed `-g` (debug information) and `-O0` (no optimization) from `CFLAGS`.
    - Added `-O2` (level 2 optimization) to both Linux and Windows builds.
    - Added `-s` (strip symbols) to both Linux and Windows builds.
- **Impact:** Both the `controller` (Linux) and `implant.exe` (Windows) are now stripped of symbol tables and optimized for performance.

## 3. Self-Contained Docker Build Environment
The build pipeline was refactored to ensure that all binary compilation and dependency management occur within Docker containers. This eliminates the "works on my machine" problem and removes the need for local toolchain installations (except for Docker).
- **Refactored `exfil-server/Dockerfile`:**
    - Added `nasm` to the builder stage.
    - Integrated the shellcode generation pipeline directly into the Docker build process.
    - Added `DOWNLOAD_TOKEN` as a build argument (`ARG`) to facilitate shellcode patching at build time.
- **Updated `docker-compose.yml`:**
    - Added `DOWNLOAD_TOKEN` to the `args` section for the `exfil-server` service.
- **Streamlined `launch.sh`:**
    - Removed local dependency checks for `make`, `mingw-w64`, `openssl`, and `python3`.
    - Removed the local shellcode build step (now handled by the Docker builder stage).
    - Kept essential runtime tasks: IP detection, token generation, and PKI generation (CA/Certs).

## 4. Verification
- Verified that `exploit.js` references were completely purged using `grep`.
- Verified that `-g` flags were removed from all build scripts.
- Verified that the `Dockerfile` correctly handles the full build lifecycle from source to stripped binary.
