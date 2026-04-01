#include "implant_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

char *operating_system_info()
{
    char *os_info = malloc(512);

#ifdef _WIN32
    // Windows specific
    OSVERSIONINFO version;
    version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&version);
    snprintf(os_info, 512, "Windows | %lu.%lu",
             version.dwMajorVersion,
             version.dwMinorVersion);

#elif __linux__ || __APPLE__
// Unix/Linux/Mac
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
