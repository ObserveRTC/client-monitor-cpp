# Compiles the libwebrtc and libmediasoupclient bindings, and examples/mediasoup_client.cpp, the
# way an application builds them: libmediasoupclient first, then client-monitor-cpp with
# CLIENTMONITOR_MEDIASOUPCLIENT=ON. CI runs it against a prebuilt libwebrtc:
#
#   cmake -S test/bindings -B build-bindings \
#     -DCLIENTMONITOR_SOURCE_DIR=$PWD \
#     -DLIBWEBRTC_INCLUDE_PATH=/path/to/webrtc/include \
#     -DLIBWEBRTC_BINARY_PATH=/path/to/webrtc/lib
#   cmake --build build-bindings
#
# It compiles and archives; it does not link an executable. Prebuilt libwebrtc is built against
# Chromium's own libc++, which a program has to link with too; an application doing so builds
# libwebrtc (or picks a prebuilt one) to match its toolchain anyway.

cmake_minimum_required(VERSION 3.14)

project(clientmonitor_bindings_check LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CLIENTMONITOR_SOURCE_DIR "" CACHE PATH "client-monitor-cpp checkout")
set(MEDIASOUPCLIENT_GIT_TAG "3.5.0" CACHE STRING "libmediasoupclient tag to build against")

if(NOT CLIENTMONITOR_SOURCE_DIR)
	get_filename_component(CLIENTMONITOR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

# 1. libmediasoupclient, which reads LIBWEBRTC_INCLUDE_PATH / LIBWEBRTC_BINARY_PATH.
FetchContent_Declare(
	mediasoupclient
	GIT_REPOSITORY https://github.com/versatica/libmediasoupclient.git
	GIT_TAG ${MEDIASOUPCLIENT_GIT_TAG}
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mediasoupclient)

# 2. client-monitor-cpp with the binding. Nothing else to set: it finds libsdptransform's json.hpp
#    and the libwebrtc include path on its own.
set(CLIENTMONITOR_MEDIASOUPCLIENT ON)
add_subdirectory("${CLIENTMONITOR_SOURCE_DIR}" clientmonitor)

# 3. The documented integration, with the warnings an application might turn into errors.
add_library(mediasoup_client_example OBJECT "${CLIENTMONITOR_SOURCE_DIR}/examples/mediasoup_client.cpp")
target_link_libraries(mediasoup_client_example PRIVATE clientmonitor::clientmonitor)

if(NOT MSVC)
	target_compile_options(mediasoup_client_example PRIVATE -Wall -Wextra -Werror)
endif()
