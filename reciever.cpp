// receiver.cpp
#include "socket_utils.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>

static constexpr std::size_t MAX_PACKET_SIZE = 1024;
static constexpr std::size_t OFFSET_SIZE     = 4;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage:\n  " << argv[0] << " <listen_port>\n";
        return 1;
    }

    int listenPort = std::stoi(argv[1]);

    if (!initSockets()) {
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::perror("socket");
        cleanupSockets();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(listenPort));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::perror("bind");
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    std::cout << "Listening on UDP port " << listenPort << "...\n";

    std::string      fileName;
    std::vector<char> fileBuffer;
    std::size_t      expectedSize = 0;
    bool             started      = false;

    std::vector<char> packet(MAX_PACKET_SIZE);

    while (true) {
        sockaddr_in senderAddr{};
        socklen_t senderLen = sizeof(senderAddr);

        int len = recvfrom(sock,
                           packet.data(),
                           static_cast<int>(packet.size()),
                           0,
                           reinterpret_cast<sockaddr*>(&senderAddr),
                           &senderLen);
        if (len == SOCKET_ERROR) {
            std::perror("recvfrom");
            break;
        }
        if (len <= 0)
            continue;

        // interpretuj paket ako string kvôli kontrolným správam
        std::string msg(packet.data(), packet.data() + len);

        if (msg.rfind("NAME=", 0) == 0) {
            fileName = msg.substr(5);
            std::cout << "Incoming file: " << fileName << "\n";
            continue;
        }

        if (msg.rfind("SIZE=", 0) == 0) {
            expectedSize = static_cast<std::size_t>(std::stoull(msg.substr(5)));
            fileBuffer.assign(expectedSize, 0);   // alokujeme buffer na celý súbor
            std::cout << "Expected size: " << expectedSize << " bytes\n";
            continue;
        }

        if (msg == "START") {
            started = true;
            std::cout << "Receiving data...\n";
            continue;
        }

        if (msg == "STOP") {
            std::cout << "STOP received.\n";
            break;
        }

        // inak je to DATA paket
        if (!started || fileBuffer.empty()) {
            std::cerr << "Received data before START/SIZE.\n";
            continue;
        }

        if (len < static_cast<int>(OFFSET_SIZE)) {
            std::cerr << "Short data packet.\n";
            continue;
        }

        std::uint32_t netOffset = 0;
        std::memcpy(&netOffset, packet.data(), OFFSET_SIZE);
        std::uint32_t offset = ntohl(netOffset);

        std::size_t dataLen = static_cast<std::size_t>(len) - OFFSET_SIZE;

        if (offset + dataLen > fileBuffer.size()) {
            std::cerr << "Packet outside bounds (offset=" << offset
                      << ", len=" << dataLen << ")\n";
            continue;
        }

        std::memcpy(fileBuffer.data() + offset,
                    packet.data() + OFFSET_SIZE,
                    dataLen);
    }

    if (!fileName.empty() && !fileBuffer.empty()) {
        std::ofstream out(fileName, std::ios::binary);
        if (!out) {
            std::cerr << "Cannot create output file '" << fileName << "'\n";
        } else {
            out.write(fileBuffer.data(), static_cast<std::streamsize>(fileBuffer.size()));
            std::cout << "Saved file '" << fileName
                      << "' (" << fileBuffer.size() << " bytes).\n";
        }
    } else {
        std::cerr << "No file received.\n";
    }

    closeSocket(sock);
    cleanupSockets();
    return 0;
}
