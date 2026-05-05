# Redirector (Machine 1)

The c2-redirector requires no Dockerfile. It uses the stock `alpine:latest` image with a single `socat` command defined inline in `docker-compose.yml`:

```
socat TCP-LISTEN:443,fork,reuseaddr TCP:c2-server:443
```

## Why no Dockerfile

socat is a raw TCP relay — it does not terminate TLS, modify packets, or add any application logic. There is nothing to build. The alpine image with one `apk add socat` is sufficient.

## Role in the infrastructure

- Sits on both `frontnet` (victim-facing) and `backnet` (internal)
- Victim connects to the c2-redirector IP (`192.168.56.X:443`)
- socat forwards every byte to `c2-server:443` on backnet
- The full mTLS handshake passes through unchanged — the c2-redirector is invisible to TLS
- `c2-server` has no host port and is unreachable directly from the victim network

## IP separation

The c2-redirector binds to a secondary IP alias (`C2_HOST_IP` in `.env`) that is distinct from the host machine's primary IP. The implant binary bakes in only the c2-redirector IP — the real C2 machine IP never appears in the binary or in victim-side network captures.

## Relationship to the exfil c2-redirector

A parallel container (`exfil-redirector`, Machine 5) applies the same pattern to the exfil channel on port 9443. See `exfil-redirector/README.md`.
