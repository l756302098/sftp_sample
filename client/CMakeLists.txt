cmake_minimum_required(VERSION 3.0)

project(sftp_client VERSION 1.0)

add_definitions(-std=c++17)

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${LIBSSH_INCLUDE_DIR}
)

add_executable(sftp_client SftpClient.cpp test_client.cpp)
target_link_libraries(sftp_client pthread ${LIBSSH_LIBRARY} boost_thread boost_system glog ssh2)