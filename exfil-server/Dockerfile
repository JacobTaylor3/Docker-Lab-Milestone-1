# Exfil + Exploit Server Container
FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    netcat-openbsd \
    nodejs \
    npm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/exploit

# Copy the Windows implant from the C2 project tree into this container
COPY c2-server/C2-Server-and-Implant/bin/windows/implant.exe ./

# Copy the exploit website files from exfil-server/CVE folder
COPY exfil-server/CVE-2021-21191---CVE-2021-21192 ./CVE-2021-21191---CVE-2021-21192

EXPOSE 8000
EXPOSE 8888

# Start CVE web app (8888) and generic file host (8000)
CMD ["/bin/sh", "-c", "cd /opt/exploit/CVE-2021-21191---CVE-2021-21192 && node server.js 8888 & cd /opt/exploit && python3 -m http.server 8000"]
