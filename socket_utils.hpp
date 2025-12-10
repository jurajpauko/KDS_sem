// socket_utils.hpp
#pragma once
#include <iostream>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socklen_t = int;

inline bool initSockets() {
    WSADATA wsaData;
    int r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != 0) {
        std::cerr << "WSAStartup failed: " << r << std::endl;
        return false;
    }
    return true;
}

inline void cleanupSockets() {
    WSACleanup();
}

inline void closeSocket(SOCKET s) {
    closesocket(s);
}

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define INVALID_SOCKET -1
#define SOCKET_ERROR  -1
using SOCKET = int;

inline bool initSockets() { return true; }

inline void cleanupSockets() {}

inline void closeSocket(SOCKET s) {
    close(s);
}

#endif
