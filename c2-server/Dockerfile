# C2 Server Container for C2-Server-and-Implant
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    make \
    mingw-w64 \
    netcat-openbsd \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/c2

# Copy the cloned project contents (Makefile + src + bin)
COPY C2-Server-and-Implant/ ./

# Build both Linux and Windows implants
RUN make

EXPOSE 4444
CMD ["./bin/linux/controller"]