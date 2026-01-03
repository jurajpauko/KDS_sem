// SENDER
#include "socket_utils.hpp"
#include "packet_definitions.h"

#include "sha256.hpp"
#include "socket_utils.hpp"
#include "crc32.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>


int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage:\n  " << argv[0] << " <target_ip> <target_port> <local_port> <file_path>\n";
        return 1;
    }

    const char *target_ip = argv[1];
    // receivers local port
    int         target_port = std::atoi(argv[2]);
    // receivers target port, we will recieve N/ACK here
    int         local_port = std::atoi(argv[3]);
    // full path to the file we want to send
    const char *file_path     = argv[4];

    if (!initSockets()) {
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::perror("socket");
        cleanupSockets();
        return 1;
    }
    // used for socket timeout
    DWORD timeout_ms = 1000;
    // recvfrom will wait max 1 second
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) < 0) {
        perror("setsockopt failed");
        closeSocket(sock);
        return 1;
    }

    sockaddr_in local{}, target{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(static_cast<uint16_t>(local_port));
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (const struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("Bind failed");
        closeSocket(sock);
        return 1;
    }

    target.sin_family      = AF_INET;
    target.sin_port        = htons(static_cast<uint16_t>(target_port));
    inet_pton(AF_INET, target_ip, &target.sin_addr);

    if (inet_pton(AF_INET, target_ip, &target.sin_addr) != 1) {
        std::cerr << "Invalid receiver IP address\n";
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    std::string file_hash;
    try {
        file_hash = sha256(file_path);
    } catch (const std::exception& e) {
        std::cerr << "SHA256 error: " << e.what() << "\n";
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file: " << file_path << "\n";
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }
    file.seekg(0, std::ios::end);
    long file_size = file.tellg();
    file.clear();
    file.seekg(0, std::ios::beg);

    const char *file_name = file_path;
    const char *last_slash = strrchr(file_path, '/');
    if (last_slash) {
        file_name = last_slash + 1;
    }

    StartPacket start_packet;
    start_packet.type = START;
    start_packet.file_size = (uint32_t)file_size;
    std::strncpy(start_packet.file_name, file_name, MAX_FILENAME_SIZE - 1);
    start_packet.file_name[MAX_FILENAME_SIZE - 1] = '\0';
    std::strncpy(start_packet.hash, file_hash.c_str(), 65);
    start_packet.hash[64] = '\0';
    
    int tries = 0;
    bool started = false;
    std::cout << "Sending START. File: " << file_name << ", Size: " << file_size << " B" 
              << ", Hash: " << file_hash << "to " << target_ip << ": " << target_port << "\n";

    while (!started) {
        // Send START packetk
        sendto(sock,reinterpret_cast<const char*>(&start_packet),sizeof(StartPacket),0,reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        ControlPacket control_packet;
        socklen_t len = sizeof(local);
        int n = recvfrom(sock,reinterpret_cast<char*>(&control_packet),sizeof(control_packet),0,reinterpret_cast<sockaddr*>(&target), &len);
        if (n < 0) {
            std::cout << "Timemout: ACK for START not recieved. Sending again...\n";
            tries++;
            if (tries > 15) {
                std::cerr << "ENDING: Did not recieve ACK for START after 15 tries.\n";
                closeSocket(sock);
                cleanupSockets();
                return 1;
            }
            continue; 
        }

        if (n == sizeof(ControlPacket) && control_packet.type == ACK && control_packet.seq_num == 0) {
            std::cout << "Recieved ACK for START. Starting to send data.\n";
            started = true;
        } else {
            std::cout << "Unexpected packet recieved. Sending START again.\n";
        }
    }

    uint32_t seq_num = 0; 
    tries = 0;
    char buffer[MAX_PAYLOAD_SIZE];
    while (true) {
        file.read(buffer, MAX_PAYLOAD_SIZE);
        size_t bytes_read = file.gcount();
        if (bytes_read <= 0) {
            std::cout << "0 bytes read.\n";
            break;
        }
        bool packet_sent = false;
        DataPacket data_packet;
        data_packet.seq_num = seq_num;
        data_packet.data_size = (uint16_t)bytes_read;
        data_packet.crc = crc32(reinterpret_cast<const uint8_t*>(buffer), bytes_read);
        std::memcpy(data_packet.payload, buffer, bytes_read);
        while (!packet_sent) {
            
            sendto(sock,reinterpret_cast<const char*>(&data_packet), sizeof(data_packet) - (MAX_PAYLOAD_SIZE - bytes_read),0,reinterpret_cast<const sockaddr*>(&target), sizeof(target));
            std::cout << "Sending packet: " << seq_num << ", data size: " << bytes_read << " B\n";
            ControlPacket control_packet;
            socklen_t len = sizeof(local);
            int n = recvfrom(sock,reinterpret_cast<char*>(&control_packet),sizeof(control_packet),0,reinterpret_cast<sockaddr*>(&target), &len);
            if (n < 0) {
                std::cout << "Timeout. Sending packet " << seq_num << " again.\n";
                tries++;
                if (tries> 15) {
                    std::cerr << "ENDING: Could not recieve ACK/NACK for paket " << seq_num << " after 15 tries.\n";
                    closeSocket(sock);
                    cleanupSockets();
                    return 1;
                }
                continue; 
            }
            if (control_packet.seq_num == seq_num) {
                if (control_packet.type == ACK) {
                    std::cout << "ACK recieved succesfully for packet " << seq_num << ".\n";
                    tries = 0;
                    seq_num++;
                    packet_sent = true;
                } else if (control_packet.type == NACK) {
                    tries = 0;
                    std::cout << "NACK recieved for packet " << seq_num << ". Trying again.\n";
                }
            } else {
                tries = 0;
                std::cout << "IGNORING: Recieved acknowledgement for wrong seq number (" << control_packet.seq_num << ").\n";
            }
        }
    }
    ControlPacket stop_packet = {STOP, seq_num};
    sendto(sock, reinterpret_cast<const char*>(&stop_packet),sizeof(ControlPacket), 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    std::cout << "STOP sent\n.";
    file.close();
    closeSocket(sock);
    cleanupSockets();
    return 0;
}
