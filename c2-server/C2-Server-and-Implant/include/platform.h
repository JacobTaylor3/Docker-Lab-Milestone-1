#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define SLEEP(x) Sleep((x) * 1000)
#define CLOSE_SOCKET(x) closesocket(x)
#define POPEN(cmd, mode) _popen(cmd, mode)
#define PCLOSE(fp) _pclose(fp)

// Windows needs socket initialization
static inline void platform_init()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void platform_cleanup()
{
    WSACleanup();
}

#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define SLEEP(x) sleep(x)
#define CLOSE_SOCKET(x) close(x)
#define POPEN(cmd, mode) popen(cmd, mode)
#define PCLOSE(fp) pclose(fp)

static inline void platform_init() {}
static inline void platform_cleanup() {}

#endif

#endif