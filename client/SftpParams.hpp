#pragma once
#include <unistd.h>
#include <string>

struct SftpParams
{
    std::string ip;
    uint8_t port;
    std::string username;
    std::string password;
    SftpParams()
    {
       port = 22; 
    }
};