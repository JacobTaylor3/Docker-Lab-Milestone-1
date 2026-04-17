# ── Stage 1: build Linux controller with OpenSSL ─────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        make \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Build context is the project root (set in docker-compose.yml context: .)
WORKDIR /opt/c2
COPY c2-server/C2-Server-and-Implant/ ./

# Build the Linux controller binary.
# implant_certs.h is NOT needed here — it is only used by implant.c (Windows).
# The controller target (Makefile) only compiles controller.c + protocol.c +
# tls.c + controller_utils.c.
RUN make bin/linux/controller

# ── Stage 2: lean runtime image ───────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
        ca-certificates \
        netcat-openbsd \
        curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/c2

COPY --from=builder /opt/c2/bin/linux/controller ./bin/linux/controller

# Bake controller cert, key, CA cert, and serial whitelist into the image.
# These are generated fresh on each launch.sh run by certs/generate_certs.sh.
# The implant cert serial number is written to whitelist.txt at generation time
# for BP4 revocation support.
COPY certs/ca.crt         ./certs/ca.crt
COPY certs/ca.key         ./certs/ca.key
COPY certs/controller.crt ./certs/controller.crt
COPY certs/controller.key ./certs/controller.key
COPY certs/whitelist.txt  ./certs/whitelist.txt

EXPOSE 4444
CMD ["./bin/linux/controller"]
