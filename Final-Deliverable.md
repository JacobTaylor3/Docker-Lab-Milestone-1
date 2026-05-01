



# Final Deliverable Grading Rubric (60 pts + 10 bonus)


The final deliverable is graded using the rubric below.
Category/Requirements (what you must demonstrate in your presentation) 
## 1. Zero-Click Exploitation (6 pts)
- Reliable remote exploit gains root without user action or notification , (No metasploit/msfconsole. Utilize proof-of-concept projects on GitHub.)

- Two-stage delivery (loader/beachhead/dropper → full payload) 

## 2. Implant Capability (15 pts)
(Off-the-shelf implants may be used as reference only. An off-the-shelf implant earns 0 points.)
- ≥ 4 built-in multi-step tasks that the implant can run on a single trigger* 
- On-demand arbitrary command execution (as root) with output returned to operator
- Persists after reboot / power loss (auto-starts itself)
- Self-destruct functionality that wipes binaries, logs, persistence mechanism, etc. 
- Meaningful file exfiltration (not just command output) 
## 3. Stealth (15 pts)
- Implant is hidden, RE-resistant, and prevents/cleans its own logs on the target system

- The payload that reaches the target should be a compiled, stripped binary—no plaintext comments,debug symbols, or hard-coded secrets.
- C2 traffic is encrypted, non-predictable, obfuscated (different than exfil), and with at least one ”hop” between operator and implant

- Exfil traffic is encrypted, non-predictable, obfuscated (different than C2), and with at least one ”hop” between operator and implant

## 4. Concurrent Command and Control (2 pts)
- Demonstrate ability to concurrently manage and task multiple implants where implants on different targets operate independently

## 5. Code Quality & Reproducibility (5 pts)
- Concise README in GitHub with project purpose, build/run steps, threat model
diagram (hosts, hops, trust boundaries)

- Single operator action triggers a full chain (not manual step-by-step) from exploit
→ implant → task → exfil

## 6. Performance & Footprint (4 pts)
- CPU/RAM profile on target (top, perfmon) < 5% sustained 2
Network throughput graph (Wireshark, pcap stats) shows low, burst-only traffic 2

---

## Implementation Status

### ✅ Category 1 — Zero-Click Exploitation (6 pts)
- CVE-2021-21220 (V8 type confusion) exploit in `utils.js` — no user action required
- Two-stage delivery: shellcode stager (`stager.asm`) downloads and executes `MicrosoftEdgeUpdate.exe` via PowerShell HTTPS; fodhelper UAC bypass elevates to Administrator before payload runs
- Single-use DOWNLOAD_TOKEN (256-bit entropy) prevents analyst URL re-use; constant-time comparison prevents timing oracle

### ✅ Category 2 — Implant Capability (15 pts)
- **≥ 4 multi-step tasks:** SCREENSHOT (session-aware, SYSTEM→user helper), KEYLOG_START/STOP/DUMP, CRED_STEAL (DPAPI Master Key + SQLite), HISTORY_STEAL (Edge + Chrome)
- **Arbitrary command execution:** `RUN_CMD` via `CreateProcess(CREATE_NO_WINDOW)` — output returned over mTLS
- **Persistence:** `schtasks /ru SYSTEM /rl HIGHEST /sc ONLOGON` via ENABLE PERSISTENCE; survives reboot; DPAPI creds stored in `C:\Users\Public` (accessible from both user and SYSTEM contexts)
- **Self-destruct (SHUTDOWN):** deletes scheduled task, marks binary for reboot-deletion via `MoveFileExA(MOVEFILE_DELAY_UNTIL_REBOOT)`, wipes `ec.dat` / `ek.dat` / `kl.dat`, removes `C:\Users\Public\MicrosoftEdge\`, clears Prefetch entries for all 4 binaries in the chain, clears 5 Windows Event Logs (Security, System, Application, PowerShell/Operational, Sysmon/Operational)
- **File exfiltration:** screenshots saved as BMP, keylogs as text, browser DB copies — all under `exfil-data/<hostname>-<ip>/` on the operator host

### ✅ Category 3 — Stealth (15 pts)
- **Hidden binary:** `-mwindows` (no console window), stripped symbols, no plaintext secrets in `.rodata` — ENROLLMENT_TOKEN XOR-obfuscated via `token_obf.h`; binary named `MicrosoftEdgeUpdate.exe` in `C:\Program Files (x86)\Microsoft\EdgeUpdate\`
- **Log/artifact cleanup:** event logs cleared on shutdown, Prefetch files deleted without spawning a child process
- **C2 traffic:** TLS 1.3 AES-256-GCM mTLS, 512-byte packet padding, jittered keepalive (20–40s) — encrypted and traffic-analysis resistant
- **Redirector hop:** victim connects to `redirector:443` (socat raw TCP) → `c2-server:443`; real C2 IP never reaches the victim binary; `c2-server` has no host port and is unreachable from `frontnet`
- **Exfil vs C2:** exfil data (screenshots, keylogs, creds) travels over the same mTLS channel as C2 commands — encrypted and indistinguishable from keepalive traffic; exfil server (`exfil-server:8443`) is a separate HTTPS endpoint used only for implant delivery

### ✅ Category 4 — Concurrent C2 (2 pts)
- Background `acceptor_thread` handles incoming connections independently of the operator UI
- Up to 8 simultaneous implant sessions; each session fully independent (separate TLS state, separate exfil directory)
- Session list shows live implants with IP, OS, hostname, persistence status; `[0] Refresh`, `[99] Shutdown All`
- Single-instance mutex (`Global\MicrosoftEdgeUpdateMtx`) prevents duplicate implant processes from the same machine

### ⚠️ Category 5 — Code Quality & Reproducibility (5 pts)
- ✅ `launch.sh` — single command triggers full chain: token generation → PKI → shellcode build → docker compose up → health check
- ✅ `ProjectOverview.md` documents build/run steps, architecture, token/crypto pipeline
- ⚠️ **TODO:** Add threat model diagram to README (hosts, hops, trust boundaries)

### ⚠️ Category 6 — Performance & Footprint (4 pts)
- ✅ Implant uses `secure_jitter_sec()` sleep between reconnects; keepalive interval is 20–40s jittered — no busy-loop CPU burn
- ⚠️ **TODO:** Capture live CPU/RAM profile on Windows target (perfmon / Task Manager) showing < 5% sustained
- ⚠️ **TODO:** Capture Wireshark pcap showing low burst-only traffic pattern during normal operation

---

