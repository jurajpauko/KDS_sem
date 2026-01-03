#pragma once

#include <openssl/evp.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <stdexcept>

inline std::string sha256(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create OpenSSL Context");
    }

    if (1 != EVP_DigestInit_ex(ctx, EVP_sha256(), NULL)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize digest");
    }

    const size_t bufferSize = 32768;
    char buffer[bufferSize];

    while (file) {
        file.read(buffer, bufferSize);
        std::streamsize n = file.gcount();
        if (n > 0) {
            if (1 != EVP_DigestUpdate(ctx, buffer, n)) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("Failed to update digest");
            }
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    if (1 != EVP_DigestFinal_ex(ctx, hash, &hash_len)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize digest");
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }

    return oss.str();
}