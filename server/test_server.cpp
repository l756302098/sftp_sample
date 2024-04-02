#include <string>
#include <boost/filesystem.hpp>
#include "SftpServer.hpp"
#include <iostream>
#include <unistd.h>

int main(){
  std::string dsa_key = boost::filesystem::path("/etc/ssh/ssh_host_dsa_key").string();
  std::string rsa_key = boost::filesystem::path("/etc/ssh/ssh_host_rsa_key").string();

  SftpServer sftp_server(dsa_key, rsa_key, "admin", "test", "6990", "127.0.0.1");
  sftp_server.start();

    sleep(60*100);
  //sftp_server.shutdown();
  std::cout << "sftp server close successfully" << std::endl;
}