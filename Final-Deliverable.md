



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

### ✅ Category 1 — Zero-Click Exploitation (COMPLETE)
- CVE-2021-21220 (V8 type confusion) exploits Chrome/Edge without user interaction
- Two-stage delivery: x64 NASM shellcode stager (PEB walk → PowerShell HTTPS download) → `MicrosoftEdgeUpdate.exe` full payload
- UAC bypass via `fodhelper` elevates implant to Administrator before execution

### ✅ Category 2 — Implant Capability (COMPLETE)
- **4+ multi-step tasks (single trigger):**
  - `CRED_STEAL`: reads Local State JSON → decodes/DPAPI-decrypts master key → copies Login Data SQLite → exfils both for Edge and Chrome
  - `HISTORY_STEAL`: copies History SQLite DB for Edge and Chrome, exfils both
  - `SCREENSHOT`: detects session context → spawns helper in user session if running as SYSTEM → GDI capture → builds BMP → exfils
  - `KEYLOG_START`: spawns background thread polling `GetAsyncKeyState`, recording to `kl.dat`; `KEYLOG_DUMP` retrieves and sends the file
- **Arbitrary command execution as root:** `RUN_CMD` runs via `hidden_popen` (`CREATE_NO_WINDOW`); implant runs as Administrator (UAC bypass) or SYSTEM (scheduled task)
- **Persistence:** `ENABLE PERSISTENCE` creates `schtasks /sc ONLOGON /ru SYSTEM /rl HIGHEST`; DPAPI-encrypted credentials survive reboot at `C:\Users\Public\MicrosoftEdge\`
- **Self-destruct (`SHUTDOWN`):** deletes scheduled task, marks binary for reboot-deletion, removes install directory, deletes credentials (`ec.dat`, `ek.dat`), deletes keylog (`kl.dat`), removes `MicrosoftEdge\` directory, clears Windows Event Logs (Security, System, Application, PowerShell/Operational, Sysmon/Operational)
- **Meaningful file exfiltration:** SQLite Login Data + master key (CRED_STEAL), SQLite History (HISTORY_STEAL), BMP screenshot, keylog text file, arbitrary file read (READ_DATA)
- **Bug fixed:** screenshot returned black image when running as SYSTEM after reboot — resolved by spawning a helper process in the active user session via `WTSQueryUserToken` + `CreateProcessAsUserA`

### ✅ Category 3 — Stealth (COMPLETE)
- **Hidden:** binary named `MicrosoftEdgeUpdate.exe` in `C:\Program Files (x86)\Microsoft\EdgeUpdate\`; scheduled task named `MicrosoftEdgeUpdate`; compiled with `-mwindows` (no console window); UAC registry key cleaned up immediately after use
- **RE-resistant:** compiled with `-O2 -s` (optimized, stripped, no debug symbols); `ENROLLMENT_TOKEN` XOR-obfuscated in binary via `token_obf.h` — does not appear as plaintext in `.rodata`; decoded onto the stack at runtime only
- **Log cleanup:** all implant files wiped on `SHUTDOWN`; Windows Event Logs (Security 4688/4698, PowerShell 4104, Sysmon) cleared via `ClearEventLogA`
- **C2 traffic:** TLS 1.3 AES-256-GCM mTLS on port 443; all packets padded to 512-byte boundaries (hides payload size); keepalive traffic at random 20–40 s intervals (hides operator activity); one network hop through `c2-server` Docker container
- **Exfil/delivery traffic:** single-use HTTPS download token (256-bit entropy, constant-time compare) served via nginx on port 8443 — separate port, protocol, and server from C2 channel; one hop through `exfil-server` Docker container

### ✅ Category 4 — Concurrent C2 (COMPLETE)
- Background `acceptor_thread` handles enrollment, HELLO, and persistence detection independently of the operator UI
- Up to 8 simultaneous implant sessions; operator selects implant by number from a live session list
- Each implant operates fully independently — `BACK` returns to the list without dropping the connection
- `Shutdown All` command (select `99`) sends `COMMAND_SHUTDOWN` to all live sessions sequentially with per-implant confirmation and result output

### ⚠️ Category 5 — Code Quality & Reproducibility (NEEDS WORK)
- `launch.sh` single-command full-chain setup (PKI → tokens → shellcode → `docker compose up --build`) ✅
- README with build/run steps exists ✅
- **TODO: Add threat model diagram** to README — must show hosts, network hops, and trust boundaries (operator → c2-server container → victim; victim → exfil-server container; PKI trust chain)

### ⚠️ Category 6 — Performance & Footprint (NEEDS WORK)
- Implant design is low-footprint: no polling loops when idle (blocking `SSL_read`), keepalive only 1 small packet per 20–40 s, no disk writes except on operator command
- **TODO: Capture and document CPU/RAM profile** on a live Windows target using Task Manager / `perfmon` — must show < 5% sustained
- **TODO: Capture network throughput graph** using Wireshark or `pcap` stats — must show low, burst-only traffic pattern
