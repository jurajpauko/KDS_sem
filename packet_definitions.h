#include <cstdint>
#include <vector>
#include <string>

#define MAX_PAYLOAD_SIZE 1024 
#define HASH_SIZE 4 
#define MAX_FILENAME_SIZE 256 

enum PacketType {
    START = 0,
    ACK = 1,
    NACK = 2,
    DATA = 3,
    STOP = 4
};

struct DataPacket {
    uint32_t seq_num; 
    uint16_t data_size; 
    uint32_t crc; 
    char payload[MAX_PAYLOAD_SIZE]; 
};

#pragma pack(push, 1)
struct StartPacket {
    PacketType type = START; 
    uint32_t file_size;
    uint32_t crc;      
    char file_name[MAX_FILENAME_SIZE]; 
    char hash[65];
};
#pragma pack(pop)

struct ControlPacket {
    PacketType type; 
    uint32_t seq_num; 
};