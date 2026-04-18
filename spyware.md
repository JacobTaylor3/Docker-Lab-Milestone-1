# Spyware Features Plan

This document outlines the spyware features to be implemented in the implant and C2 controller.

## 1. Screenshot Capture (`COMMAND_SCREENSHOT`)
**Objective:** Capture the victim's current screen and exfiltrate it to the C2 server.

- **Implant Side:**
  - Use GDI/GDI+ to capture the primary monitor.
  - Save the capture to a memory buffer (or temporary file) as a BMP/JPG.
  - Send the data back as a `COMMAND_RESPONSE`.
- **Controller Side:**
  - Add a new menu option for screenshots.
  - Receive the image data and save it to the `exfil-data/` directory with a timestamp.

## 2. Keylogger (`COMMAND_KEYLOG_START`, `COMMAND_KEYLOG_STOP`, `COMMAND_KEYLOG_DUMP`)
**Objective:** Record keystrokes on the victim's machine.

- **Implant Side:**
  - `COMMAND_KEYLOG_START`: Start a background thread that uses `SetWindowsHookEx` (WH_KEYBOARD_LL) or `GetAsyncKeyState` polling to record keystrokes.
  - `COMMAND_KEYLOG_STOP`: Stop the recording thread.
  - `COMMAND_KEYLOG_DUMP`: Exfiltrate the log file and clear it.
- **Controller Side:**
  - Add menu options to start, stop, and dump the keylogger.
  - Save the log output to `exfil-data/`.

## 3. Clipboard Capture (`COMMAND_CLIPBOARD_GET`)
**Objective:** Capture the current contents of the victim's clipboard.

- **Implant Side:**
  - Use `OpenClipboard`, `GetClipboardData(CF_TEXT)`, and `CloseClipboard`.
- **Controller Side:**
  - Add menu option to retrieve clipboard text.

## 4. Browser Credential Harvesting (`COMMAND_CRED_STEAL`)
**Objective:** Steal saved login credentials from popular browsers (Chrome/Edge).

- **Implant Side:**
  - Locate the `Login Data` SQLite database for Edge/Chrome.
  - Copy it to a temporary location (to avoid file locks).
  - Use `CryptUnprotectData` (DPAPI) to decrypt the passwords.
  - Since Chrome 80+, this also requires the `Local State` file to decrypt the Master Key.
- **Controller Side:**
  - Add menu option to trigger credential harvesting.
  - Display or save the harvested credentials.

---

## Implementation Strategy

1.  **Update `protocol.h`**: Add new opcodes to `Command` enum.
2.  **Update `implant.c`**: Implement the new command handlers.
3.  **Update `controller.c`**: Update the interactive menu and response handling.
4.  **Update `platform.h`**: Add any necessary Windows-specific includes (e.g., `<gdiplus.h>` for screenshots).
5.  **Update `Makefile`**: Ensure any new libraries are linked (e.g., `-lgdi32 -lgdiplus`).
