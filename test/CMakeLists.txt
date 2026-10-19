cmake_minimum_required(VERSION 3.14)

include(FetchContent)

set(
	SOURCE_FILES
	src/tests.cpp
	src/helpers.cpp
	src/Attachments.test.cpp
	src/ClientMonitor.test.cpp
	src/Detectors.test.cpp
	src/IssueRegistry.test.cpp
	src/Monitors.test.cpp
	src/OutboundSenderBinding.test.cpp
	src/Schema.test.cpp
	src/SlicedWindow.test.cpp
	src/StatsAdapter.test.cpp
	src/StatsAdapterPipeline.test.cpp
)

add_executable(test_clientmonitor ${SOURCE_FILES})

message(STATUS "Fetching Catch2")
FetchContent_Declare(
	catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v2.13.10
	GIT_SHALLOW TRUE
	# Only the single header is used: point at a directory without a CMakeLists.txt so Catch2's own
	# (which declares an old cmake_minimum_required and warns on new CMake) is never configured.
	SOURCE_SUBDIR single_include
)
FetchContent_MakeAvailable(catch2)

target_include_directories(test_clientmonitor PRIVATE
	include
	${catch2_SOURCE_DIR}/single_include/catch2
)

target_link_libraries(test_clientmonitor PRIVATE clientmonitor::clientmonitor)

add_test(NAME clientmonitor COMMAND test_clientmonitor)
