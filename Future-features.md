
---

## ⚠️ TODO — Separate C2 and Exfil Channels (Rubric Category 3)

**Rubric requirement:**
> "C2 traffic is encrypted, non-predictable, obfuscated **(different than exfil)**"
> "Exfil traffic is encrypted, non-predictable, obfuscated **(different than C2)**"

**Current state — the gap:**
Right now, C2 commands AND all exfil data (screenshots, keylogs, credentials, history) travel over the **same** mTLS connection on port 443. From a network observer's perspective, a heartbeat packet and a screenshot upload are indistinguishable only because of the 512-byte padding — they use the same channel, same port, same certificate.

The rubric explicitly requires C2 and exfil to be **different** channels. As it stands, they are not.

**What needs to change:**
- C2 commands (heartbeat, run_cmd, set_sleep, shutdown, etc.) stay on `port 443` mTLS as-is
- Exfil data (screenshots, keylogs, credentials, history) should be sent over a **separate channel** — for example, HTTPS POST to `exfil-server:8443` — a different port, different certificate, different protocol than the C2 beacon

**Why this matters:**
- A defender correlating traffic sees two distinct patterns: a low-volume periodic beacon on 443, and occasional larger HTTPS uploads on 8443
- Neither channel alone reveals the full picture — C2 channel shows commands but not data, exfil channel shows uploads but not commands
- Satisfies the rubric's explicit requirement for channel separation

**Proposed implementation:**
- On `SCREENSHOT`, `KEYLOG_DUMP`, `CRED_STEAL`, `HISTORY_STEAL`: instead of sending data back over the mTLS C2 socket, the implant opens a separate HTTPS connection to `exfil-server:8443` and POSTs the data there
- `exfil-server` receives and saves the file directly (no c2-server involvement)
- C2 channel only returns a short acknowledgement ("exfil sent") — no bulk data

---

## TODO

Exfil server is currently doing:  ┌────────────────────────┬────────┬────────────────────────────────────┐
  │          What          │  Port  │                How                 │
  ├────────────────────────┼────────┼────────────────────────────────────┤
  │ Serve exploit webpage  │ 8888   │ Node.js static server              │
  ├────────────────────────┼────────┼────────────────────────────────────┤
  │ Serve implant download │ 8443   │ nginx + single-use token           │
  ├────────────────────────┼────────┼────────────────────────────────────┤
  │ Store exfil data       │ (none) │ Shared volume mount with c2-server

The "exfil- server" container should not be doing all these; it should just be storing the data. Possibly make a new container for the exploit webpage and the implant download and seperate containers mimicking a real world with real different hardware.
