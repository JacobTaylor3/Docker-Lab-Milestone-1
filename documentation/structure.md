# Project Structure & Rubric Alignment

This document explains the five-container architecture, how each component is wired together, and which rubric requirement each design decision satisfies.

---

## Container Architecture

The lab runs five Docker containers, each representing a distinct "machine" in the attack infrastructure. This separation is intentional — it mirrors how a real threat actor would distribute infrastructure to avoid a single point of failure and to prevent victim traffic analysis from revealing the true C2 machine.

```
                        ┌─────────────────────────────────────────────────────┐
                        │  HOST MACHINE  (192.168.56.1)                       │
                        │                                                     │
                        │  IP alias: 192.168.56.10 → redirector only         │
                        │                                                     │
                        │  ┌───────────────────── frontnet ─────────────────┐ │
                        │  │                                                 │ │
  VICTIM                │  │  [Machine 1] redirector    :443  socat relay   │ │
  192.168.56.4  ────────┼──┼──►  [Machine 3] delivery-server  :8443  nginx  │ │
                        │  │  [Machine 4] exploit-server :8888  Node.js     │ │
                        │  │  [Machine 5] exfil-receiver :9443  HTTPS sink  │ │
                        │  │                                                 │ │
                        │  └──────────────── backnet ─────────────────────┐ │ │
                        │                   │                              │ │ │
                        │            [Machine 2] c2-server :443  mTLS     │ │ │
                        │                   │  (no host port — internal)  │ │ │
                        │                   └────────────────────────────┘ │ │
                        │                                                   └─┘ │
                        └─────────────────────────────────────────────────────┘
```

| # | Container | Host Port | Protocol | Role |
|---|-----------|-----------|----------|------|
| Machine 1 | `redirector` | `192.168.56.10:443` | TCP relay (socat) | Victim-facing hop — hides real C2 IP |
| Machine 2 | `c2-server` | none (backnet only) | mTLS 1.3 | Operator command channel |
| Machine 3 | `delivery-server` | `192.168.56.10:8443` | HTTPS (nginx) | Single-use token implant download |
| Machine 4 | `exploit-server` | `192.168.56.10:8888` | HTTP (Node.js) | CVE-2021-21220 exploit webpage |
| Machine 5 | `exfil-receiver` | `192.168.56.10:9443` | HTTPS (Node.js) | Data sink for screenshots, keylogs, creds |

---

## The Two Traffic Channels

The rubric (Category 3) explicitly requires that C2 traffic and exfil traffic are **different channels** — encrypted, non-predictable, and distinguishable from each other. This is implemented as two completely separate network paths:

### Channel 1 — C2 Command Channel (port 443)
- **Protocol:** TLS 1.3, mutual authentication (mTLS), AES-256-GCM
- **Path:** victim → `redirector:443` (socat raw TCP relay) → `c2-server:443` (backnet only)
- **Payload:** small fixed-size packets (512 bytes padded), jittered keepalive every 20–40 seconds
- **Purpose:** operator sends commands, implant returns short text acknowledgements only
- **Key property:** `c2-server` has no host port mapping — it is physically unreachable from the victim network. Only the redirector can reach it via the internal `backnet` Docker bridge.

### Channel 2 — Exfil Channel (port 9443)
- **Protocol:** HTTPS (TLS), one-way (implant POSTs, receiver saves)
- **Path:** victim → `exfil-receiver:9443` (frontnet, direct)
- **Payload:** raw binary blobs (BMP screenshots, text keylogs, SQLite DB copies)
- **Purpose:** bulk data upload — never touches the C2 connection
- **Key property:** A separate container (`exfil-receiver`) and a separate port. The C2 mTLS connection never carries file data; the exfil channel never carries commands.

**Why two channels satisfies the rubric:** The two channels have different TLS certificate chains, different ports, different packet sizes, different cadences (keepalive vs burst-on-command), and route through different containers. A network observer cannot correlate them without knowing both endpoints.

---

## Component Breakdown

### Machine 1 — Redirector (`redirector/`)

`socat TCP-LISTEN:443,fork TCP:c2-server:443`

A single-line socat relay. It does **not** terminate TLS — raw TCP bytes pass through unchanged, so the full mTLS handshake happens end-to-end between the implant and `c2-server`. The redirector sees only encrypted bytes.

**Rubric — Category 3 (Stealth):** Provides the required "at least one hop between operator and implant." The implant binary bakes in the redirector's IP (`192.168.56.10`), never the real C2 machine IP (`192.168.56.1`). If the redirector is burned, you can point socat at a new C2 without recompiling the implant.

---

### Machine 2 — C2 Server (`c2-server/`)

The mTLS listener. Runs the operator controller UI (`controller`) inside the container. Accessible only via `backnet` — no host port binding means Docker never exposes it on any host interface.

**Rubric — Category 3 (Stealth):** Real C2 IP is never visible to the victim or in the implant binary. All victim-facing traffic terminates at the redirector.

**Rubric — Category 4 (Concurrent C2):** Background `acceptor_thread` handles incoming connections independently of the operator UI. Up to 8 simultaneous sessions, each with independent TLS state and exfil directory.

---

### Machine 3 — Delivery Server (`exfil-server/Dockerfile` → `delivery-runtime` stage)

nginx serves `MicrosoftEdgeUpdate.exe` (the implant binary) at `/update/<DOWNLOAD_TOKEN>`. A Node.js token server invalidates the token after the first successful download.

**Rubric — Category 1 (Zero-Click):** Supports the two-stage delivery chain: shellcode stager downloads the payload from this server using a baked-in single-use token.

**Rubric — Category 3 (Stealth):** Single-use token (256-bit entropy) prevents an analyst from re-downloading the payload using a captured URL.

---

### Machine 4 — Exploit Server (`exfil-server/Dockerfile` → `exploit-runtime` stage)

Node.js static server hosting the CVE-2021-21220 (V8 type confusion) exploit page. The shellcode is patched at build time to include the delivery server IP and download token.

**Rubric — Category 1 (Zero-Click):** Delivers the exploit that triggers code execution without user interaction. `launch.sh` patches shellcode into `utils.js` automatically at build time.

---

### Machine 5 — Exfil Receiver (`exfil-receiver/`)

Node.js HTTPS server. Accepts `POST /exfil/<hostname>/<filename>`, sanitizes the path, and writes files to `/opt/exfil/` which is mounted to `exfil-data/` on the host.

**Rubric — Category 2 (Implant Capability):** Fulfils "meaningful file exfiltration" — screenshots (BMP), keylog dumps (text), browser credential DBs and master keys, browser history DBs are all saved under `exfil-data/<hostname>/`.

**Rubric — Category 3 (Stealth):** Satisfies the requirement that exfil traffic is a **different channel** from C2 traffic. Different port (9443 vs 443), different TLS certificate, different container, different cadence.

---

## Build Pipeline

`launch.sh` triggers the entire chain with a single command:

```
launch.sh
  ├── Prompt for IP alias (192.168.56.10) + add alias to vboxnet0
  ├── Generate ENROLLMENT_TOKEN, DOWNLOAD_TOKEN (256-bit hex)
  ├── certs/generate_certs.sh → CA, c2-server, nginx, exfil-receiver certs
  └── docker compose up --build
        ├── openssl-win stage   → cross-compile OpenSSL 3.0.9 for Windows
        ├── builder stage       → compile implant.exe + patch shellcode into utils.js
        ├── delivery-runtime    → nginx + token_server.js + MicrosoftEdgeUpdate.exe
        ├── exploit-runtime     → Node.js + patched exploit webpage
        └── exfil-receiver      → Node.js HTTPS POST sink
```

**Rubric — Category 5 (Reproducibility):** Single operator action (`./launch.sh`) builds all certificates, tokens, binaries, and containers. No manual steps between exploit and exfil.

---

## Network Segmentation

Two Docker bridge networks enforce the topology at the OS level:

| Network | Members | Purpose |
|---------|---------|---------|
| `frontnet` | redirector, delivery-server, exploit-server, exfil-receiver | Victim-facing services |
| `backnet` | redirector, c2-server | Internal C2 path only |

`c2-server` is on `backnet` only — it has no route to `frontnet` and no host port. Even if an analyst compromises `exfil-receiver` or `delivery-server`, they cannot reach `c2-server` directly.

---

## Rubric Summary

| Category | Pts | Status | Key components |
|----------|-----|--------|----------------|
| 1. Zero-Click Exploitation | 6 | ✅ | `exploit-server` (CVE-2021-21220), shellcode stager, `delivery-server` (single-use token) |
| 2. Implant Capability | 15 | ✅ | screenshot, keylog, cred steal, history steal, RUN_CMD, persistence, SHUTDOWN, `exfil-receiver` |
| 3. Stealth | 15 | ✅ | mTLS + redirector hop (C2), WinHTTP + separate container (exfil), stripped binary, XOR token |
| 4. Concurrent C2 | 2 | ✅ | `acceptor_thread`, 8 sessions, session menu |
| 5. Code Quality | 5 | ⚠️ | `launch.sh` one-command build ✅ — threat model diagram TODO |
| 6. Performance | 4 | ⚠️ | Jittered keepalive ✅ — live perfmon/Wireshark capture TODO |
