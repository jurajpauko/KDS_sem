// sender.cpp
#include "socket_utils.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>

static constexpr std::size_t MAX_PACKET_SIZE = 1024;  // podľa zadania
static constexpr std::size_t OFFSET_SIZE     = 4;     // uint32_t offset

// text controll packet
bool sendTextPacket(SOCKET sock, const sockaddr_in &addr, const std::string &text) {
    int sent = sendto(sock, text.c_str(), static_cast<int>(text.size()), 0,
                      reinterpret_cast<const sockaddr*>(&addr),
                      sizeof(addr));
    if (sent == SOCKET_ERROR) {
        std::perror("sendto (control)");
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage:\n  " << argv[0]
                  << " <receiver_ip> <receiver_port> <file_path>\n";
        return 1;
    }

    std::string receiverIp   = argv[1];
    int         receiverPort = std::stoi(argv[2]);
    std::string filePath     = argv[3];

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
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(receiverPort));

    if (inet_pton(AF_INET, receiverIp.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "Invalid receiver IP address\n";
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    // open file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Cannot open file: " << filePath << "\n";
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // file name
    std::string fileName;
    std::size_t pos = filePath.find_last_of("/\\");
    if (pos == std::string::npos)
        fileName = filePath;
    else
        fileName = filePath.substr(pos + 1);

    std::cout << "Sending file '" << fileName
              << "' (" << fileSize << " bytes) to "
              << receiverIp << ":" << receiverPort << "\n";

    // 1) NAME
    if (!sendTextPacket(sock, addr, "NAME=" + fileName)) {
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    // 2) SIZE
    if (!sendTextPacket(sock, addr, "SIZE=" + std::to_string(fileSize))) {
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    // 3) START
    if (!sendTextPacket(sock, addr, "START")) {
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    // 4) DATA PACKETS
    std::vector<char> buffer(MAX_PACKET_SIZE);
    std::uint32_t offset = 0;

    while (true) {
        file.read(buffer.data() + OFFSET_SIZE,
                  static_cast<std::streamsize>(MAX_PACKET_SIZE - OFFSET_SIZE));
        std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0)
            break;

        std::uint32_t netOffset = htonl(offset);
        std::memcpy(buffer.data(), &netOffset, OFFSET_SIZE);

        std::size_t packetSize = OFFSET_SIZE + static_cast<std::size_t>(bytesRead);

        int sent = sendto(sock,
                          buffer.data(),
                          static_cast<int>(packetSize),
                          0,
                          reinterpret_cast<const sockaddr*>(&addr),
                          sizeof(addr));
        if (sent == SOCKET_ERROR) {
            std::perror("sendto (data)");
            break;
        }

        offset += static_cast<std::uint32_t>(bytesRead);
    }

    // 5) STOP
    sendTextPacket(sock, addr, "STOP");

    std::cout << "Done.\n";

    closeSocket(sock);
    cleanupSockets();
    return 0;
}
