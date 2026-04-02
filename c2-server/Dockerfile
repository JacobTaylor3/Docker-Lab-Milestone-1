# Stage 1: Build Linux controller only
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/c2
COPY C2-Server-and-Implant/ ./
RUN make bin/linux/controller

# Stage 2: Lean runtime image — no compilers
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    netcat-openbsd \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/c2
COPY --from=builder /opt/c2/bin/linux/controller ./bin/linux/controller

EXPOSE 4444
CMD ["./bin/linux/controller"]
