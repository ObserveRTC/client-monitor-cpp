# A minimal application consuming client-monitor-cpp the way the README says to, through
# FetchContent. CI builds it against the checked-out sources:
#
#   cmake -S test/consumer -B build-consumer -DCLIENTMONITOR_SOURCE_DIR=$PWD
#   cmake --build build-consumer && ./build-consumer/consumer
#
# Without CLIENTMONITOR_SOURCE_DIR it fetches the tagged release from GitHub instead.

cmake_minimum_required(VERSION 3.14)

project(clientmonitor_consumer LANGUAGES CXX)

include(FetchContent)

set(CLIENTMONITOR_SOURCE_DIR "" CACHE PATH "Use a local client-monitor-cpp checkout")
set(CLIENTMONITOR_GIT_TAG "main" CACHE STRING "Tag to fetch when no local checkout is given")

if(CLIENTMONITOR_SOURCE_DIR)
	FetchContent_Declare(clientmonitor SOURCE_DIR "${CLIENTMONITOR_SOURCE_DIR}")
else()
	FetchContent_Declare(
		clientmonitor
		GIT_REPOSITORY https://github.com/ObserveRTC/client-monitor-cpp.git
		GIT_TAG ${CLIENTMONITOR_GIT_TAG}
		GIT_SHALLOW TRUE
	)
endif()

FetchContent_MakeAvailable(clientmonitor)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE clientmonitor::clientmonitor)

# Pulled in as a dependency, the library must not build its own tests.
if(TARGET test_clientmonitor)
	message(FATAL_ERROR "client-monitor-cpp built its tests as a dependency")
endif()
