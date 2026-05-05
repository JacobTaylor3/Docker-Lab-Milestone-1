# Exfil Redirector (Machine 5)

The exfil c2-redirector requires no Dockerfile. It uses the stock `alpine:latest` image with a single `socat` command defined inline in `docker-compose.yml`:

```
socat TCP-LISTEN:9443,fork,reuseaddr TCP:exfil-receiver:9443
```

## Why no Dockerfile

socat is a raw TCP relay — it does not terminate TLS, modify packets, or add any application logic. There is nothing to build. The alpine image with one `apk add socat` is sufficient.

## Role in the infrastructure

- Sits on both `frontnet` (victim-facing) and `exfilnet` (internal)
- Implant POSTs exfil data to the c2-redirector IP (`192.168.56.X:9443`)
- socat forwards every byte to `exfil-receiver:9443` on exfilnet
- The full TLS handshake passes through unchanged — the c2-redirector is invisible to TLS
- `exfil-receiver` has no host port and is unreachable directly from the victim network

## IP separation

The exfil c2-redirector binds to the same IP alias as the C2 c2-redirector (`C2_HOST_IP` in `.env`). From the victim's perspective both c2-redirectors appear to be the same machine on different ports (`:443` C2, `:9443` exfil) — the real exfil-receiver IP never appears in victim-side network captures.

## Relationship to the C2 c2-redirector

This container mirrors `c2-redirector` (Machine 1) exactly, but for the exfil channel instead of the C2 channel:

| | C2 c2-redirector | Exfil c2-redirector |
|-|---------------|-----------------|
| Listens | `:443` | `:9443` |
| Forwards to | `c2-server:443` (backnet) | `exfil-receiver:9443` (exfilnet) |
| Network | frontnet + backnet | frontnet + exfilnet |
