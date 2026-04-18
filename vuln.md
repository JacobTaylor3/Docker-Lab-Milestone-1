
# Capstone Security Analysis — Remaining Tasks & Roadmap

This document tracks the vulnerabilities and forensic gaps that are **not yet mitigated** in the current pipeline and defines the roadmap for future implementation.

---

## 1. Process Tree Obfuscation

**Vulnerability:** The current exploit chain (`chrome.exe` → `powershell.exe`) is a high-confidence indicator of compromise (IOC) for any EDR or Sysmon-equipped endpoint.
**Required Implementation:**
- **Parent PID (PPID) Spoofing:** Modify the stager or implant to spawn the next process under a legitimate parent (e.g., `explorer.exe` or `svchost.exe`) using `UpdateProcThreadAttribute`.
- **Process Hollowing / Injection:** Instead of spawning a new process, inject the implant directly into a running, trusted process.

---

## 2. UAC Bypass Diversification

**Vulnerability:** The `fodhelper` bypass relies on a visible registry write to `HKCU`, which leaves forensic artifacts and is easily flagged by Sysmon Event 13.
**Required Implementation:**
- **Alternative Bypasses:** Implement bypasses that do not rely on the `ms-settings` key (e.g., `ComputerDefaults.exe` or `DiskCleanup` bypasses).
- **Mock Trusted Folders:** Explore DLL hijacking in "trusted" directories to bypass UAC without registry modifications.

---

## 3. C2 Infrastructure — Dead Drop Resolver

**Vulnerability:** The C2 IP is currently baked into the binary. While encrypted/obfuscated, it is a static target that can be blocked or taken down.
**Required Implementation:**
- **Dynamic Resolution:** Update the implant to fetch its C2 IP from a public platform (e.g., a GitHub Gist, Twitter Bio, or Reddit post) at runtime.
- **Traffic Blending:** Ensure the resolver request matches the traffic pattern of a normal user browsing that platform.

---

## 4. TLS Mimicry (JA3 Fingerprinting)

**Vulnerability:** The current OpenSSL handshake produces a unique JA3 fingerprint that distinguishes the implant from standard browsers like Chrome or Edge.
**Required Implementation:**
- **Fingerprint Matching:** Modify the TLS client parameters (cipher suite ordering, extensions, and versions) to exactly match the JA3 signature of the victim's primary browser.

---

## 5. Advanced Credential Obfuscation

**Vulnerability:** While DPAPI protects the keys from offline extraction, a local Administrator can still use `CryptUnprotectData` to recover the PEM files.
**Required Implementation:**
- **Custom Entropy:** Add an application-specific "secret" to the DPAPI call to prevent other local processes from decrypting the credentials.
- **In-Memory Transformation:** Implement a custom transformation of the cert/key in memory so they never appear as valid PEM even if the DPAPI layer is bypassed.

