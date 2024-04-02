#include  "SftpClient.h"
#include <glog/logging.h>

#define SLOG(MODE) LOG(MODE) << "[Server][SFTP] "


SftpClient& SftpClient::instance()
{
    static SftpClient singleton;
    return singleton;
}

SftpClient::SftpClient()
:inited(false),
started(false),
working(false),
sock(-1),
session(nullptr),
sftp(nullptr)
{
}

SftpClient::~SftpClient()
{
    if(inited)
    {
        libssh2_exit();
    }
}

bool SftpClient::init()
{
    SLOG(INFO) << __func__;
    if(inited)
    {
        return true;
    }
    int rc = libssh2_init(0);
    if (rc != 0) 
    {
        SLOG(ERROR) <<"Libssh2 initialization failed";
        return false;
    }
    inited = true;
    return true;
}

bool SftpClient::startClient(std::shared_ptr<SftpParams> params)
{
    SLOG(INFO) << __func__;
    if(!inited)
    {
        SLOG(ERROR) << "Not inited";
        return false;
    }
    if(started)
    {
        return true;
    }
    // 建立 TCP 连接
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sock) 
    {
        SLOG(ERROR) << "Socket failed";
        return false;
    }
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(params->port);
    sin.sin_addr.s_addr = inet_addr(params->ip.c_str());
    if (connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) 
    {
        SLOG(ERROR) << "Connect failed";
        exceptionHandle();
        return false;
    }

    // 建立 SFTP 会话
    session = libssh2_session_init();
    if (!session) 
    {
        SLOG(ERROR) << "Libssh2_session_init failed";
        exceptionHandle();
        return false;
    }
    int rc = libssh2_session_handshake(session, sock);
    if (rc != 0) 
    {
        SLOG(ERROR) << "Libssh2_session_handshake failed";
        exceptionHandle();
        return false;
    }

    //登录
    rc = libssh2_userauth_password(session, params->username.c_str(), params->password.c_str());
    if (rc != 0) 
    {
        SLOG(ERROR) << "Libssh2_userauth_password failed";
       exceptionHandle();
        return false;
    }

    //初始化传输会话
    sftp = libssh2_sftp_init(session);
    if (!sftp) 
    {
        SLOG(ERROR) << "Libssh2_sftp_init failed";
        exceptionHandle();
        return false;
    }

    started = true;
    SLOG(INFO) << "Succeed to start sftp client.";
    return true;
}

bool SftpClient::stopClient()
{
    SLOG(INFO) << __func__;
    if(started)
    {
        if(working)
        {
            working = false;
            if(taskThread && taskThread->joinable()) 
            {
                taskThread->join();
            }
            // 关闭 SFTP 会话和其他资源
            exceptionHandle();
        }
        started = false;
        SLOG(INFO) << "Succeed to stop sftp client";
        return true;
    }
    else
    {
        SLOG(ERROR) << "Failed to stop sftp client";
        return false;
    }
}

bool SftpClient::uploadTask(std::string &localPath, std::string &remotePath, std::function<void(int)> transferedCallback)
{
    SLOG(INFO) << __func__ << ", localPath : " << localPath << ", remotePath : " << remotePath;

    if(localPath.empty() || remotePath.empty())
    {
        SLOG(ERROR) << "File path error";
        return false;
    }
    if(!started)
    {
        SLOG(ERROR) << "Sftp client not started";
        return false;
    }
    if(working)
    {
        SLOG(ERROR) << "In working";
        return false;
    }
    working = true;
    this->transferedCallback = transferedCallback;
    transferred = 0;
    if(taskThread && taskThread->joinable()) 
    {
        taskThread->join();
    }
    taskThread.reset( new std::thread( [this, &localPath, &remotePath](){
        SLOG(INFO) << "Start upload thread. localPath : " << localPath << ", remotePath : " << remotePath;
        // LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(sftp, remotePath.c_str(), 
        //     LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT, 
        //     LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
        // if (!handle) 
        // {
        //     SLOG(ERROR) << "Libssh2_sftp_open failed";
        //     this->startTaskFiled();
        //     return;
        // }

        LIBSSH2_SFTP_HANDLE *handle = nullptr;

        libssh2_uint64_t totalSize = 0; //本地文件大小
        libssh2_uint64_t uploadSize = 0; //已上传的大小

        //续传判断
        if(this->isContinueTransfer(localPath, remotePath))
        {
            handle = libssh2_sftp_open(sftp, remotePath.c_str(), LIBSSH2_FXF_WRITE, 0);
            if (!handle) 
            {
                SLOG(ERROR) << "Libssh2_sftp_open failed";
                this->startTaskFiled();
                return;
            }
            //获取远程文件信息
            LIBSSH2_SFTP_ATTRIBUTES remoteFileAttrs;
            if (libssh2_sftp_stat(sftp, remotePath.c_str(), &remoteFileAttrs) == 0) 
            {
                uploadSize = remoteFileAttrs.filesize;
                libssh2_sftp_seek64(handle, uploadSize);
                SLOG(INFO) << "Has been uploaded size: " << uploadSize;
            }
            else
            {
                SLOG(ERROR) << "Failed to get remote file attributes. file path: " << remotePath;
            }
        }
        else
        {
            handle = libssh2_sftp_open(sftp, remotePath.c_str(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0);
            if (!handle) 
            {
                SLOG(ERROR) << "Libssh2_sftp_open failed";
                this->startTaskFiled();
                return;
            }
        }

        std::ifstream fin(localPath, std::ios::binary);
        if (!fin.good()) 
        {
            SLOG(ERROR) << "Failed to open local file";
            libssh2_sftp_close(handle);
            this->startTaskFiled();
            return;
        }
        // 获取文件大小
        fin.seekg(0, std::ios_base::end);
        totalSize = fin.tellg();
        SLOG(INFO) << "Local file size: " << totalSize;
        if(0 == totalSize)
        {
            SLOG(ERROR) << "Local file is empty";
            libssh2_sftp_close(handle);
            this->startTaskFiled();
            fin.close();
            return;
        }
        fin.seekg(uploadSize, std::ios_base::beg);

        size_t readed = 0; //读取到的大小
        ssize_t writeed = 0; //已写入的大小
        char *ptr = nullptr; //写入缓存
        libssh2_uint64_t bufferLen = 1024;
        char buffer[bufferLen]; //存储读取的内容
        libssh2_uint64_t loaded = uploadSize; //已加载到待上传buf中的大小
        do 
        {
            //加载文件
            if(0 >= totalSize - loaded)
            {
                break;
            }
            if(totalSize - loaded <= bufferLen)
            {
                fin.read(buffer, totalSize - loaded);
                readed = totalSize - loaded;
                loaded = totalSize;
            }
            if(totalSize - loaded > bufferLen)
            {
                fin.read(buffer, bufferLen);
                readed = bufferLen;
                loaded += bufferLen;
            }

            ptr = buffer;
            do 
            {
                writeed = libssh2_sftp_write(handle, ptr, readed);
                if(writeed < 0)
                {
                    break;
                }
                ptr += writeed;
                readed -= writeed;
                uploadSize += writeed;
                //进度
                this->transferProgress(totalSize, uploadSize);
            } while(readed);
            
        } while(this->working && writeed > 0);

        fin.close();
        libssh2_sftp_close(handle);
        if(totalSize == uploadSize)
        {
            this->removeFile(localPath);
            SLOG(INFO) << "Succeed to upload file. ";
        }
        else
        {
            if(this->transferedCallback)
            {
                this->transferedCallback(-1);
            }
            this->updateTransferFile(localPath, remotePath);
            SLOG(INFO) << "Failed to upload file. TotalSize : " << totalSize << ", uploadSize : " << uploadSize;
        }
        this->working = false;
        SLOG(INFO) << "End upload thread." ;
    }));

    return true;
}

bool SftpClient::downloadTask(std::string &localPath, std::string &remotePath, std::function<void(int)> transferedCallback)
{
    SLOG(INFO) << __func__ << ", localPath : " << localPath << ", remotePath : " << remotePath;

    if(localPath.empty() || remotePath.empty())
    {
        SLOG(ERROR) << "File path error";
        return false;
    }
    if(!started)
    {
        SLOG(ERROR) << "Sftp client not started";
        return false;
    }
    if(working)
    {
        SLOG(ERROR) << "In working";
        return false;
    }
    working = true;
    this->transferedCallback = transferedCallback;
    transferred = 0;
    if(taskThread && taskThread->joinable()) 
    {
        taskThread->join();
    }
    taskThread.reset( new std::thread( [this, &localPath, &remotePath](){
        SLOG(INFO) << "Start download thread. localPath : " << localPath << ", remotePath : " << remotePath;
        LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(sftp, remotePath.c_str(), LIBSSH2_FXF_READ, 0);
        if (!handle) 
        {
            SLOG(ERROR) << "Libssh2_sftp_open failed";
            this->startTaskFiled();
            return;
        }

        libssh2_uint64_t totalSize = 0; //待下载文件大小
        libssh2_uint64_t downloadedSize = 0; //已下载的大小
        std::ofstream fout;
        //续传判断
        if(this->isContinueTransfer(localPath, remotePath))
        {
            //获取本地文件信息
            struct stat localFileAttrs;
            if(0 == stat(localPath.c_str(), &localFileAttrs))
            {
                downloadedSize = localFileAttrs.st_size;
                SLOG(INFO) << "Local file size: " << downloadedSize;
            }
            else
            {
                SLOG(ERROR) << "Failed to getFileInfo. file path: " << localPath;
            }
        }

        if(downloadedSize != 0)
        {
            fout.open(localPath, std::ios::app | std::ios::binary);//追加方式打开
            libssh2_sftp_seek64(handle, downloadedSize);
        }
        else
        {
            fout.open(localPath, std::ios::out | std::ios::binary);
        }
        if (!fout.is_open())
        {
            SLOG(ERROR) << "Failed to open local file";
            libssh2_sftp_close(handle);
            this->startTaskFiled();
            fout.close();
            return;
        }
        SLOG(INFO) << "Local file size: " << fout.tellp();

        // 获取远程文件属性
        LIBSSH2_SFTP_ATTRIBUTES remoteFileAttrs;
        if (libssh2_sftp_stat(sftp, remotePath.c_str(), &remoteFileAttrs) == 0) 
        {
            totalSize = remoteFileAttrs.filesize;
            // 输出文件大小
            SLOG(INFO) << "Remote file size: " << remoteFileAttrs.filesize;
        } 
        else 
        {
            SLOG(ERROR) << "Failed to get file attributes.";
        }

        char buffer[1024];
        int len = 0;
        if(downloadedSize < totalSize)
        {
            while(this->working && (len = libssh2_sftp_read(handle, buffer, sizeof(buffer))) > 0) 
            {
                fout.write(buffer, len);
                downloadedSize += len;
                //进度
                this->transferProgress(totalSize, downloadedSize);
            }
        }
        fout.seekp(0, std::ios_base::end);
        fout.close();
        libssh2_sftp_close(handle);
        if(totalSize == downloadedSize)
        {
            this->removeFile(localPath);
            SLOG(INFO) << "Succeed to download file.";
        }
        else
        {
            if(this->transferedCallback)
            {
                this->transferedCallback(-1);
            }
            this->updateTransferFile(localPath, remotePath);
            SLOG(INFO) << "Fail to dowanload file. TotalSize : " << totalSize << ", uploadSize : " << downloadedSize;
        }
        this->working = false;
        SLOG(INFO) << "End download thread." ;
    }));

    return true;
}

bool SftpClient::cancelTask()
{
    SLOG(INFO) << __func__;
    if(working)
    {
        working = false;
        if(taskThread && taskThread->joinable()) 
        {
            taskThread->join();
        }
        SLOG(INFO) << "Succeed to cancel task";
        return true;
    }
    else
    {
        SLOG(ERROR) << "Failed to cancel task";
        return false;
    }
}

void SftpClient::transferProgress(libssh2_uint64_t totalSize, libssh2_uint64_t transferredSize)
{
    if(0 == totalSize)
    {
        return;
    }
    int p = ((transferredSize*1.0) / (totalSize)*1.0) * 100;
    if(this->transferred != p && transferedCallback)
    {
        this->transferred = p;
        transferedCallback(transferred);
    }
}

void SftpClient::SftpClient::exceptionHandle()
{
    if(sftp)
    {
        libssh2_sftp_shutdown(sftp);
    }
    if(session) 
     {
        libssh2_session_disconnect(session, "Normal Shutdown");
        libssh2_session_free(session);
    }
    if(sock != LIBSSH2_INVALID_SOCKET) 
    {
        shutdown(sock, 2);
        close(sock);
    }
}

void SftpClient::startTaskFiled()
{
    SLOG(ERROR) << "Failed to start task.";
    this->working = false;
    if(this->transferedCallback)
    {
        this->transferedCallback(-1);
    }
}

bool SftpClient::isContinueTransfer(std::string &localPath, std::string &remotePath)
{
    //判断传输信息文件是否存在
    std::string transferInfoFilePath = this->getTransferInfoFilePath(localPath);
    if(!isFileExists(transferInfoFilePath))
    {
        SLOG(INFO) << "TransferInfoFile not exist";
        return false;
    }

    //读取传输文件
    std::string remotePathInFile = "";
    long remoteFileMTime = 0;
    std::string localPathInFile = "";
    long localFileMTime = 0;
    /*
    try
    {
        std::ifstream infile(transferInfoFilePath);
        json  jsonData =  json::parse(infile);
        remotePathInFile = jsonData.at("remote_path");
        remoteFileMTime = jsonData.at("remote_file_m_time");
        localPathInFile = jsonData.at("local_path");
        localFileMTime = jsonData.at("local_file_m_time");
    }
    catch(nlohmann::json::exception& e)
    {
        SLOG(ERROR) << e.what();
        return false;
    }
    catch(...)
    {
        SLOG(ERROR) <<"Unknown exception.";
        return false;
    }
    */
    //1. 比较文件名
    if(0 != localPath.compare(localPathInFile) || 0 != remotePathInFile.compare(remotePathInFile))
    {
        SLOG(ERROR) <<"File name mismatch";
        return false;
    }

    //获取远程文件信息
    LIBSSH2_SFTP_ATTRIBUTES remoteFileAttrs;
    if (libssh2_sftp_stat(sftp, remotePath.c_str(), &remoteFileAttrs) != 0) 
    {
        SLOG(ERROR) << "Failed to get file attributes.";
        return false;
    }
    //获取本地文件信息
    struct stat localFileAttrs;
    if(stat(localPath.c_str(), &localFileAttrs) != 0)
    {
        SLOG(ERROR) << "Failed to getFileInfo. file path: " << localPath;
        return false;
    }

    //2. 比较修改时间
    if((unsigned long)remoteFileMTime != remoteFileAttrs.mtime || localFileMTime != localFileAttrs.st_mtim.tv_sec)
    {
        SLOG(ERROR) <<"T time mismatch";
        return false;
    }

    return true;
}

std::string SftpClient::getTransferInfoFilePath(std::string &localPath)
{
    int pos = localPath.find_last_of('/');
    std::string dir = localPath.substr(0, pos);
    std::string filePath = dir + "/transferInfo.json";
    SLOG(INFO) <<"TransferInfoFilePath : " << filePath;
    return filePath;
}

bool SftpClient::updateTransferFile(std::string &localPath, std::string &remotePath)
{
    std::string transferInfoFilePath = this->getTransferInfoFilePath(localPath);
    //获取远程文件信息
    LIBSSH2_SFTP_ATTRIBUTES remoteFileAttrs;
    if (libssh2_sftp_stat(sftp, remotePath.c_str(), &remoteFileAttrs) != 0) 
    {
        SLOG(ERROR) << "Failed to get remote file attributes. file path: " << remotePath;
        return false;
    }
    //获取本地文件信息
    struct stat localFileAttrs;
    if(stat(localPath.c_str(), &localFileAttrs) != 0)
    {
        SLOG(ERROR) << "Failed to get local file attributes. file path: " << localPath;
        return false;
    }
    /*
    json data = json::object();
    data["remote_path"] = remotePath;
    data["remote_file_m_time"] = remoteFileAttrs.mtime;
    data["local_path"] = localPath;
    data["local_file_m_time"] = localFileAttrs.st_mtim.tv_sec;
    try
    {
        std::ofstream outfile(transferInfoFilePath);
        outfile << data.dump(4);
        return true;
    }
    catch (const std::exception& e)
    {
        SLOG(ERROR) << "Failed to save data to '" << transferInfoFilePath << "'. " << e.what();
        return false;
    }
    */
   return true;
}

bool SftpClient::isFileExists(std::string &filePath)
{
    return (0 == access(filePath.c_str(), F_OK));
}

bool SftpClient::removeFile(std::string &filePath)
{
    return 0 == remove(getTransferInfoFilePath(filePath).c_str());
}