cmake_minimum_required(VERSION 3.20)

project(jolt_polygon_runner LANGUAGES CXX)

set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)

if("${CMAKE_CXX_FLAGS_DISTRIBUTION}" STREQUAL "")
	set(CMAKE_CXX_FLAGS_DISTRIBUTION "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING
		"Flags used by the CXX compiler during Distribution builds." FORCE)
endif()
if("${CMAKE_EXE_LINKER_FLAGS_DISTRIBUTION}" STREQUAL "")
	set(CMAKE_EXE_LINKER_FLAGS_DISTRIBUTION "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING
		"Flags used by the linker during Distribution builds." FORCE)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT JOLT_RUNNER_IPO_SUPPORTED OUTPUT JOLT_RUNNER_IPO_ERROR)
if(NOT JOLT_RUNNER_IPO_SUPPORTED)
	message(FATAL_ERROR "Distribution IPO is required: ${JOLT_RUNNER_IPO_ERROR}")
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Build" "${CMAKE_BINARY_DIR}/jolt")

file(GLOB JOLT_CASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../cases/*_case.cpp")
add_executable(jolt_polygon_runner
	jolt_polygon_runner.cpp
	jolt_result_writer.cpp
	jolt_runner_args.cpp
	../cases/jolt_case_registry.cpp
	${JOLT_CASE_SOURCES}
)
if(MSVC)
	set_property(TARGET jolt_polygon_runner PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
target_include_directories(jolt_polygon_runner PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
	"${CMAKE_CURRENT_LIST_DIR}/.."
	"${CMAKE_CURRENT_LIST_DIR}/../cases"
	"${CMAKE_CURRENT_LIST_DIR}/../common"
	"${CMAKE_CURRENT_LIST_DIR}/../../rendering/include"
)
target_link_libraries(jolt_polygon_runner PRIVATE Jolt)
set_property(TARGET jolt_polygon_runner
	PROPERTY INTERPROCEDURAL_OPTIMIZATION_DISTRIBUTION TRUE)
target_compile_features(jolt_polygon_runner PRIVATE cxx_std_17)
