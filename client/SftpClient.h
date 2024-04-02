#pragma once
#include <iostream>
#include <fstream>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <sys/stat.h>
#include "SftpParams.hpp"

class SftpClient
{
public:
    static SftpClient& instance();
    ~SftpClient();
    bool init();
    bool startClient(std::shared_ptr<SftpParams> params);
    bool stopClient();
    bool uploadTask(std::string &localPath, std::string &remotePath, std::function<void(int)> transferedCallback = nullptr);
    bool downloadTask(std::string &localPath, std::string &remotePath, std::function<void(int)> transferedCallback = nullptr);
    bool cancelTask();

private:
    SftpClient();
    void transferProgress(libssh2_uint64_t totalSize, libssh2_uint64_t transferredSize);
    void exceptionHandle();
    void startTaskFiled();

    bool isContinueTransfer(std::string &localPath, std::string &remotePath);
    std::string getTransferInfoFilePath(std::string &localPath);
    bool updateTransferFile(std::string &localPath, std::string &remotePath);
    bool isFileExists(std::string &filePath);
    bool removeFile(std::string &filePath);
    
private:
    int  transferred;  //记录进度百分比，当前进度与该值不等时，调用回调
    std::function<void(int)> transferedCallback;//进度回调

private:
    bool inited;
    bool started;
    std::atomic_bool working;
    int sock;
    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp;
    std::shared_ptr<std::thread> taskThread;
};