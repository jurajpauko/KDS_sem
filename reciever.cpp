// RECIEVER
#define NOMINMAX
#include <openssl/sha.h>
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
    if (argc != 4) {
        std::cerr << "Usage:\n  " << argv[0] << " <local_port> <target_port> <target_ip>\n";
        return 1;
    }
    // localing on this port - senders target port
    int local_port = std::stoi(argv[1]);
    // sending ack to this port - senders local port
    int target_port = std::stoi(argv[2]);
    const char *target_ip = argv[3];

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
    sockaddr_in recv_addr{};
    socklen_t   recv_len = sizeof(recv_addr);
    sockaddr_in local{}, target{};
    local.sin_family      = AF_INET; // IPv4
    local.sin_port        = htons(static_cast<uint16_t>(local_port)); // Convert host byte order - Little/Big endian
    local.sin_addr.s_addr = htonl(INADDR_ANY); 
    target.sin_family      = AF_INET;
    target.sin_port        = htons(static_cast<uint16_t>(target_port));
    inet_pton(AF_INET, target_ip, &target.sin_addr); // we will send N/ACK to this IP and this port

    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        std::perror("bind");
        closeSocket(sock);
        cleanupSockets();
        return 1;
    }

    std::cout << "Localing on UDP port " << local_port << ". Waiting for start packet...\n";

    std::ofstream file; 
    StartPacket start_packet;
    socklen_t len_start = sizeof(target);
    bool started = false;

    char tmp_buffer[sizeof(StartPacket) + 10];
    // waiting for start packet - max wait time = 1 second, max tries = 15
    int tries = 0;
    while (!started) {
        int n = recvfrom(sock,reinterpret_cast<char*>(&tmp_buffer),sizeof(tmp_buffer),0,reinterpret_cast<sockaddr*>(&recv_addr),&recv_len);
        if (n < 0) {
            tries++;
            if (tries > 15) {
                std::cerr << "CLOSING: No start packet received after 15 tries\n";
                closeSocket(sock);
                cleanupSockets();
                return 1;
            }
            continue; 
        }
        PacketType received_type = *(PacketType*)tmp_buffer;
        if (received_type == START) {
            std::memcpy(&start_packet, tmp_buffer, std::min((size_t)n, sizeof(StartPacket)));
            std::cout << "Start packet recieved. Filename: " << start_packet.file_name 
                      << ", Size: " << start_packet.file_size << " B\n";

            file.open(("received/" + std::string(start_packet.file_name)).c_str(),std::ios::binary | std::ios::out);
            if (!file.is_open()) {
                std::cerr << "ERROR: Could not open file: " << start_packet.file_name << "\n";
                closeSocket(sock);
                cleanupSockets();
                return 1;
            }
            ControlPacket ack_packet = {ACK, 0}; 
            sendto(sock,reinterpret_cast<const char*>(&ack_packet),sizeof(ack_packet),0,reinterpret_cast<const sockaddr*>(&target),len_start);
            std::cout << "ACK sent, ready to recieve data.\n";
            started = true;
        } else {
            std::cout << "Unexpected packet type: " << received_type << " was recieved. Waiting for START packet.\n";
        }
    }

    socklen_t len = len_start;
    uint32_t seq_num = 0; 
    tries = 0;
    DataPacket data_packet;
    while (true) {
        int n = recvfrom(sock,reinterpret_cast<char*>(&data_packet),sizeof(data_packet),0,reinterpret_cast<sockaddr*>(&recv_addr),&recv_len);
        if (n < 0) {
            std::perror("Failed to recieve packet, trying again...");
            tries++;
            if (tries > 15) {
                std::cerr << "CLOSING: No data packet recieved after 15 tries.\n";
                closeSocket(sock);
                cleanupSockets();
                return 1;
            }
        }

        if (n == sizeof(ControlPacket) && ((ControlPacket*)&data_packet)->type == STOP) {
            std::cout << "CLOSING: STOP packet recieved\n";
            break;
        }

        if (n == sizeof(StartPacket)) {
            ControlPacket ack_packet = {ACK, 0}; 
            sendto(sock,reinterpret_cast<const char*>(&ack_packet),sizeof(ControlPacket),0,reinterpret_cast<const sockaddr*>(&target),len);
            std::cout << "ACK sent, ready to recieve data.\n";
            continue;
        }

        if (data_packet.seq_num == seq_num) {
            if (data_packet.data_size > MAX_PAYLOAD_SIZE || n < (int)(sizeof(DataPacket) - MAX_PAYLOAD_SIZE + data_packet.data_size)) {
                std::cerr << "IGNORING: invalid packet size.\n";
                continue;
            }
            uint32_t calc_crc = crc32(reinterpret_cast<const uint8_t*>(data_packet.payload),data_packet.data_size);
            if (calc_crc == data_packet.crc) {
                file.write(data_packet.payload, data_packet.data_size);
                ControlPacket ack_packet = {ACK, data_packet.seq_num};
                sendto(sock,reinterpret_cast<const char*>(&ack_packet),sizeof(ControlPacket),0,reinterpret_cast<const sockaddr*>(&target),len);
                std::cout << "Packet " << seq_num << " recieved succesfully. ACK sent.\n";
                tries = 0;
                seq_num++; 
            } else {
                ControlPacket nack_packet = {NACK, data_packet.seq_num};
                sendto(sock,reinterpret_cast<const char*>(&nack_packet),sizeof(ControlPacket),0,reinterpret_cast<const sockaddr*>(&target),len);
                tries = 0;
                std::cout << "CRC missmatch for packet " << data_packet.seq_num << ". NACK sent.\n";
            }
        } else {
            ControlPacket ack_packet = {ACK, data_packet.seq_num};
            sendto(sock,reinterpret_cast<const char*>(&ack_packet),sizeof(ControlPacket),0,reinterpret_cast<const sockaddr*>(&target),len);
            std::cout << "Duplicite packet " << data_packet.seq_num << " recieved. ACK sent again.\n";
        }      

    }

    file.close();
    std::string received_file_hash = sha256(start_packet.file_name);
    std::cout << "File transmitted. SHA-256 hash of the recieved file: " << received_file_hash << ".\n";
    std::cout << "Expected hash: " << start_packet.hash << ".\n";
    std::cout << (received_file_hash == std::string(start_packet.hash) ? "Hash match" : "Hash missmatch") << ".\n";
    closeSocket(sock);
    cleanupSockets();
    return 0;
}
