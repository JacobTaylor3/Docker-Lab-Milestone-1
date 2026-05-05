# Spyware Features Status & Roadmap

This document tracks the implemented spyware capabilities and outlines future features for the implant and C2 controller.

## ✅ Implemented Features

### 1. Screenshot Capture (`COMMAND_SCREENSHOT`)
*   **Status:** Functional
*   **Logic:** Captures the full primary monitor using GDI.
*   **Storage:** Exfiltrated as a `.bmp` file to `exfil-data/`.
*   **Modular Source:** `spyware.c` -> `spy_screenshot_capture()`

### 2. Keylogger (`COMMAND_KEYLOG_START/STOP/DUMP`)
*   **Status:** Functional
*   **Logic:** Background thread using `GetAsyncKeyState` polling to record keystrokes to `kl.dat`.
*   **Formatting:** Includes special keys like `[ENTER]`, `[BACKSPACE]`, and `[SHIFT]`.
*   **Modular Source:** `spyware.c` -> `keylogger_thread()`

### 3. Clipboard Capture (`COMMAND_CLIPBOARD_GET`)
*   **Status:** Functional
*   **Logic:** Accesses the Windows clipboard to retrieve text data.
*   **Modular Source:** `spyware.c` -> `spy_clipboard_get()`

### 4. Browser Credential Harvesting (`COMMAND_CRED_STEAL`)
*   **Status:** Functional (Edge & Chrome)
*   **Logic:** 
    1. Decrypts the browser Master Key using DPAPI (`CryptUnprotectData`).
    2. Copies the `Login Data` SQLite database to bypass file locks.
    3. Exfiltrates the key and database for offline decryption.
*   **Analysis Tool:** `utils-scripts/decrypt_creds.py`

### 5. Browser History Harvesting (`COMMAND_HISTORY_STEAL`)
*   **Status:** Functional (Edge & Chrome)
*   **Logic:** Copies and exfiltrates the non-encrypted `History` SQLite databases.
*   **Analysis Tool:** `utils-scripts/read_history.py`

---

## 🚀 Future Roadmap


### 1


    *   Recursive search through `C:\Users\` for sensitive file extensions (`.docx`, `.pdf`, `.kdbx`, `.xlsx`).
*   **Messaging Exfiltration:** 
    *   Target Discord, Telegram, and Signal local databases (similar to browser harvesting).



