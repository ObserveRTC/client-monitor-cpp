cmake_minimum_required(VERSION 3.14)

project(clientmonitor
	VERSION 0.2.0
	DESCRIPTION "ObserveRTC client monitor for native WebRTC (libwebrtc / libmediasoupclient)"
	HOMEPAGE_URL "https://github.com/ObserveRTC/client-monitor-cpp"
	LANGUAGES CXX
)

include(FetchContent)

# Extracted archives get the extraction time, so a re-download triggers a rebuild.
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()

# Whether this is the project being built, or a dependency pulled in by FetchContent /
# add_subdirectory. Tests and examples default on only for the former.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set(CLIENTMONITOR_IS_TOP_LEVEL ON)
else()
	set(CLIENTMONITOR_IS_TOP_LEVEL OFF)
endif()

# Project options.
option(CLIENTMONITOR_BUILD_TESTS     "Build unit tests" ${CLIENTMONITOR_IS_TOP_LEVEL})
option(CLIENTMONITOR_BUILD_EXAMPLES  "Build examples" ${CLIENTMONITOR_IS_TOP_LEVEL})
option(CLIENTMONITOR_LOG_TRACE       "Enable CM_LOG_TRACE (See Logger.hpp)" OFF)
option(CLIENTMONITOR_LIBWEBRTC       "Build the libwebrtc binding (needs LIBWEBRTC_INCLUDE_PATH)" OFF)
option(CLIENTMONITOR_MEDIASOUPCLIENT "Build the libmediasoupclient binding (implies CLIENTMONITOR_LIBWEBRTC)" OFF)
option(CLIENTMONITOR_FETCH_JSON      "Fetch nlohmann_json when no nlohmann_json target or package is found" ON)

# nlohmann::json, first match wins:
#  1. CLIENTMONITOR_JSON_INCLUDE_PATH: a directory with a flat <json.hpp>. Point it at
#     libsdptransform's include dir in an app using libmediasoupclient, so both libraries compile
#     against the very same json.
#  2. An nlohmann_json::nlohmann_json target the parent project already has.
#  3. find_package(nlohmann_json).
#  4. FetchContent (CLIENTMONITOR_FETCH_JSON).
set(CLIENTMONITOR_JSON_INCLUDE_PATH "" CACHE STRING "Directory with an existing flat json.hpp to use")

if(CLIENTMONITOR_MEDIASOUPCLIENT)
	set(CLIENTMONITOR_LIBWEBRTC ON)

	# libmediasoupclient's headers include libsdptransform's flat <json.hpp>. Compiling the
	# binding against any other nlohmann::json would put two versions of the same classes in
	# one program, so default to that one. libsdptransform is fetched inside libmediasoupclient's
	# directory scope, where its *_SOURCE_DIR variable is not visible from here; its FetchContent
	# properties are global.
	if(NOT CLIENTMONITOR_JSON_INCLUDE_PATH)
		FetchContent_GetProperties(libsdptransform SOURCE_DIR CLIENTMONITOR_SDPTRANSFORM_DIR)

		if(CLIENTMONITOR_SDPTRANSFORM_DIR AND EXISTS "${CLIENTMONITOR_SDPTRANSFORM_DIR}/include/json.hpp")
			set(CLIENTMONITOR_JSON_INCLUDE_PATH "${CLIENTMONITOR_SDPTRANSFORM_DIR}/include")
		elseif(TARGET mediasoupclient)
			# libmediasoupclient lists libsdptransform's include directory among its public ones.
			get_target_property(CLIENTMONITOR_MSC_INCLUDES mediasoupclient INTERFACE_INCLUDE_DIRECTORIES)

			foreach(dir IN LISTS CLIENTMONITOR_MSC_INCLUDES)
				if(dir AND NOT dir MATCHES "\\$<" AND EXISTS "${dir}/json.hpp")
					set(CLIENTMONITOR_JSON_INCLUDE_PATH "${dir}")
					break()
				endif()
			endforeach()
		endif()

		if(NOT CLIENTMONITOR_JSON_INCLUDE_PATH)
			message(WARNING
				"CLIENTMONITOR_MEDIASOUPCLIENT is ON but libsdptransform's json.hpp was not found. "
				"Set CLIENTMONITOR_JSON_INCLUDE_PATH to the directory holding it, so the binding and "
				"libmediasoupclient compile against the same nlohmann::json.")
		endif()
	endif()
endif()

if(CLIENTMONITOR_JSON_INCLUDE_PATH AND NOT EXISTS "${CLIENTMONITOR_JSON_INCLUDE_PATH}/json.hpp")
	message(FATAL_ERROR "CLIENTMONITOR_JSON_INCLUDE_PATH (${CLIENTMONITOR_JSON_INCLUDE_PATH}) has no json.hpp")
endif()

message(STATUS "client-monitor-cpp ${PROJECT_VERSION}")
message(STATUS "  CLIENTMONITOR_BUILD_TESTS       : ${CLIENTMONITOR_BUILD_TESTS}")
message(STATUS "  CLIENTMONITOR_BUILD_EXAMPLES    : ${CLIENTMONITOR_BUILD_EXAMPLES}")
message(STATUS "  CLIENTMONITOR_LIBWEBRTC         : ${CLIENTMONITOR_LIBWEBRTC}")
message(STATUS "  CLIENTMONITOR_MEDIASOUPCLIENT   : ${CLIENTMONITOR_MEDIASOUPCLIENT}")
message(STATUS "  CLIENTMONITOR_JSON_INCLUDE_PATH : ${CLIENTMONITOR_JSON_INCLUDE_PATH}")

# Version header, generated into the build tree.
set(CLIENTMONITOR_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/include")
configure_file(
	"${PROJECT_SOURCE_DIR}/cmake/version.hpp.in"
	"${CLIENTMONITOR_GENERATED_INCLUDE_DIR}/clientmonitor/version.hpp"
	@ONLY
	NEWLINE_STYLE LF
)

set(
	SOURCE_FILES
	src/ClientMonitor.cpp
	src/Logger.cpp
	src/StatsAdapters.cpp
	src/detectors/Detectors.cpp
	src/detectors/DryInboundTrackDetector.cpp
	src/detectors/DryOutboundTrackDetector.cpp
	src/detectors/IceDisconnectedDetector.cpp
	src/detectors/StatsGapDetector.cpp
	src/detectors/TransportDelayDetector.cpp
	src/monitors/IceMonitors.cpp
	src/monitors/MediaMonitors.cpp
	src/monitors/PeerConnectionMonitor.cpp
	src/monitors/RtpMonitors.cpp
	src/monitors/TrackMonitors.cpp
	src/utils/IntervalTimer.cpp
	src/utils/IssueRegistry.cpp
	src/utils/MediaMath.cpp
	src/utils/SlicedWindow.cpp
	src/utils/Utils.cpp
)

if(CLIENTMONITOR_LIBWEBRTC)
	list(APPEND SOURCE_FILES bindings/libwebrtc/LibWebRtcBinding.cpp)
endif()

if(CLIENTMONITOR_MEDIASOUPCLIENT)
	list(APPEND SOURCE_FILES bindings/mediasoupclient/MediasoupClientBinding.cpp)
endif()

add_library(clientmonitor STATIC ${SOURCE_FILES})
add_library(clientmonitor::clientmonitor ALIAS clientmonitor)

target_compile_features(clientmonitor PUBLIC cxx_std_17)
set_target_properties(clientmonitor PROPERTIES
	CXX_EXTENSIONS OFF
	POSITION_INDEPENDENT_CODE ON
)

if(CLIENTMONITOR_LOG_TRACE)
	target_compile_definitions(clientmonitor PRIVATE CM_LOG_TRACE=1)
endif()

# Warnings for our own sources only, never for the consumer's.
if(MSVC)
	target_compile_options(clientmonitor PRIVATE /W3)
else()
	target_compile_options(clientmonitor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Public headers: #include <clientmonitor/ClientMonitor.hpp>.
target_include_directories(clientmonitor PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CLIENTMONITOR_GENERATED_INCLUDE_DIR}>
	$<INSTALL_INTERFACE:include>
)

# nlohmann::json.
if(CLIENTMONITOR_JSON_INCLUDE_PATH)
	# Shim <nlohmann/json.hpp> onto the provided flat json.hpp.
	set(JSON_SHIM_DIR "${PROJECT_BINARY_DIR}/json-shim")
	file(WRITE "${JSON_SHIM_DIR}/nlohmann/json.hpp" "#pragma once\n#include <json.hpp>\n")
	target_include_directories(clientmonitor PUBLIC
		$<BUILD_INTERFACE:${JSON_SHIM_DIR}>
		$<BUILD_INTERFACE:${CLIENTMONITOR_JSON_INCLUDE_PATH}>
	)
else()
	if(NOT TARGET nlohmann_json::nlohmann_json)
		find_package(nlohmann_json 3.10 QUIET)
	endif()

	if(NOT TARGET nlohmann_json::nlohmann_json)
		if(NOT CLIENTMONITOR_FETCH_JSON)
			message(FATAL_ERROR "nlohmann_json not found and CLIENTMONITOR_FETCH_JSON is OFF")
		endif()

		message(STATUS "Fetching nlohmann_json")
		set(JSON_BuildTests OFF CACHE INTERNAL "")
		set(JSON_Install OFF CACHE INTERNAL "")
		FetchContent_Declare(
			nlohmann_json
			URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
			URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
		)
		FetchContent_MakeAvailable(nlohmann_json)
	endif()

	target_link_libraries(clientmonitor PUBLIC nlohmann_json::nlohmann_json)
endif()

find_package(Threads REQUIRED)
target_link_libraries(clientmonitor PUBLIC Threads::Threads)

if(CLIENTMONITOR_LIBWEBRTC)
	if(NOT TARGET mediasoupclient AND NOT LIBWEBRTC_INCLUDE_PATH)
		message(FATAL_ERROR "CLIENTMONITOR_LIBWEBRTC needs LIBWEBRTC_INCLUDE_PATH (or the mediasoupclient target)")
	endif()

	if(LIBWEBRTC_INCLUDE_PATH)
		# SYSTEM: libwebrtc's and abseil's headers do not build warning-free under -Wextra
		# -Wpedantic, and an application compiling with -Werror must not fail on them.
		target_include_directories(clientmonitor SYSTEM PUBLIC
			"${LIBWEBRTC_INCLUDE_PATH}"
			"${LIBWEBRTC_INCLUDE_PATH}/third_party/abseil-cpp"
		)
		target_compile_definitions(clientmonitor PUBLIC
			$<$<NOT:$<PLATFORM_ID:Windows>>:WEBRTC_POSIX>
			$<$<PLATFORM_ID:Windows>:WEBRTC_WIN>
			$<$<PLATFORM_ID:Windows>:NOMINMAX>
			$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
			$<$<PLATFORM_ID:Darwin>:WEBRTC_MAC>
		)
	endif()

	if(LIBWEBRTC_BINARY_PATH)
		target_link_libraries(clientmonitor PUBLIC ${LIBWEBRTC_BINARY_PATH}/libwebrtc${CMAKE_STATIC_LIBRARY_SUFFIX})
	endif()
endif()

if(CLIENTMONITOR_MEDIASOUPCLIENT)
	if(NOT TARGET mediasoupclient)
		message(FATAL_ERROR "CLIENTMONITOR_MEDIASOUPCLIENT needs the mediasoupclient target: add libmediasoupclient before client-monitor-cpp")
	endif()

	# libmediasoupclient's headers are included as <Transport.hpp>, <Producer.hpp>, ...
	target_link_libraries(clientmonitor PUBLIC mediasoupclient)
	target_include_directories(clientmonitor PUBLIC $<BUILD_INTERFACE:$<TARGET_PROPERTY:mediasoupclient,SOURCE_DIR>/include>)
endif()

if(CLIENTMONITOR_BUILD_TESTS)
	enable_testing()
	add_subdirectory(test)
endif()

if(CLIENTMONITOR_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
