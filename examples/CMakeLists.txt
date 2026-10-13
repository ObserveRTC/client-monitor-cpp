cmake_minimum_required(VERSION 3.14)

# Normally built from the repository root (CLIENTMONITOR_BUILD_EXAMPLES, on by default there).
# Configured on its own (`cmake -S examples -B build`), it pulls the library in from the parent
# directory the way an application would.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(clientmonitor_examples LANGUAGES CXX)
	add_subdirectory(.. clientmonitor)
endif()

add_executable(basic_monitor basic_monitor.cpp)
target_link_libraries(basic_monitor PRIVATE clientmonitor::clientmonitor)
