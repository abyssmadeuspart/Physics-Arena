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

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Build" "${CMAKE_BINARY_DIR}/jolt")

add_executable(jolt_polygon_runner
	jolt_polygon_runner.cpp
	jolt_result_writer.cpp
	jolt_runner_args.cpp
	jolt_shared_visual_mode.cpp
	../cases/jolt_case_registry.cpp
	../cases/jolt_box_container_pile_10k_case.cpp
)
if(MSVC)
	set_property(TARGET jolt_polygon_runner PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
target_include_directories(jolt_polygon_runner PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
	"${CMAKE_CURRENT_LIST_DIR}/.."
	"${CMAKE_CURRENT_LIST_DIR}/../cases"
	"${CMAKE_CURRENT_LIST_DIR}/../../rendering/include"
)
target_link_libraries(jolt_polygon_runner PRIVATE Jolt)
if(WIN32)
	target_link_libraries(jolt_polygon_runner PRIVATE ws2_32)
endif()
target_compile_features(jolt_polygon_runner PRIVATE cxx_std_17)
