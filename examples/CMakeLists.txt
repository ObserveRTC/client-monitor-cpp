cmake_minimum_required(VERSION 3.14)

add_executable(basic_monitor basic_monitor.cpp)
target_link_libraries(basic_monitor PRIVATE clientmonitor::clientmonitor)
