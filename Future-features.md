## Bug
* When the user refresh the page, the expliot more than once, so there are mulitple implants connects to the C2 from the smae machine.
* The commmand prompt windows on the the victim would appear split seconds on shutdown command.

---

## ✅ C2 Traffic Redirector (Docker) — IMPLEMENTED

**Problem:** The implant previously connected directly to the `c2-server` container, meaning the C2 IP was both baked into the binary and exposed on the network. A single firewall rule or takedown would kill the entire operation.

**Proposed Implementation — socat redirector container:**

Add a third Docker container (`redirector`) that acts as a dumb TCP relay between the victim and the real C2 server. The implant bakes in the redirector's IP, not the C2's. The redirector pipe-forwards raw TCP bytes to `c2-server:443`. Because it is a raw forward (not a TLS terminator), the full mTLS handshake passes through end-to-end unchanged — no certificate modifications are required.

```
Victim → redirector:443 (socat raw TCP forward) → c2-server:443
```

**docker-compose.yml changes:**
- Add a `redirector` service using a lightweight Alpine image
- Install `socat` and run: `socat TCP-LISTEN:443,fork TCP:c2-server:443`
- Place `redirector` on a `frontnet` bridge (victim-facing) and `c2-server` on a `backnet` bridge (operator-facing only); `redirector` sits on both
- Map only `redirector:443` to the host — `c2-server` becomes unreachable directly from the victim network

**Implant change:**
- `C2_HOST_IP` passed at build time becomes the redirector's IP, not the c2-server's IP
- No changes to TLS, enrollment, or protocol code

**Operational benefits:**
- The binary never contains the real C2 IP — only the redirector IP
- If the redirector is burned (blocked/identified), point socat at a new C2 without recompiling the implant
- Creates a genuine two-hop network path: victim → redirector → c2-server
- Satisfies the rubric's "at least one hop between operator and implant" requirement with real network separation

---

## C2 Infrastructure — Dead Drop Resolver

**Vulnerability:** The C2 IP is currently baked into the binary. While encrypted/obfuscated, it is a static target that can be blocked or taken down.
**Required Implementation:**
- **Dynamic Resolution:** Update the implant to fetch its C2 IP from a public platform (e.g., a GitHub Gist, Twitter Bio, or Reddit post) at runtime.
- **Traffic Blending:** Ensure the resolver request matches the traffic pattern of a normal user browsing that platform.
---


