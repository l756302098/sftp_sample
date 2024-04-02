#include <string>
#include <boost/filesystem.hpp>
#include "SftpClient.h"
#include <iostream>
#include <unistd.h>
#include "SftpParams.hpp"

int main(){
  SftpClient::instance().init();
  SftpParams params;
  params.ip = "127.0.0.1";
  params.port = 22;
  params.username = "admin";
  params.password = "123";
  SftpClient::instance().startClient(std::make_shared<SftpParams>(params));
  
  std::cout << "sftp server close successfully" << std::endl;
}