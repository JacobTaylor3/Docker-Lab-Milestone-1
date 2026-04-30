



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

