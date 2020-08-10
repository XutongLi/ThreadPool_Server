cmake_minimum_required(VERSION 3.10)
project(ThreadPool_SVR)

set(CMAKE_CXX_STANDARD 11)

add_executable(ThreadPool_SVR main.cpp  http_conn.cpp)

find_package(Threads)

target_link_libraries(ThreadPool_SVR ${CMAKE_THREAD_LIBS_INIT})