cmake_minimum_required(VERSION 3.14)

# Normally built from the repository root (CLIENTMONITOR_BUILD_EXAMPLES, on by default there).
# Configured on its own (`cmake -S examples -B build`), it pulls the library in from the parent
# directory the way an application would.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	project(clientmonitor_examples LANGUAGES CXX)
	add_subdirectory(.. clientmonitor)
endif()

foreach(example basic_monitor api_tour)
	add_executable(${example} ${example}.cpp)
	target_link_libraries(${example} PRIVATE clientmonitor::clientmonitor)

	if(NOT MSVC)
		target_compile_options(${example} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
endforeach()
