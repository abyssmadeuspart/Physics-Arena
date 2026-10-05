cmake_minimum_required(VERSION 3.22)

project(box3d_polygon_runner LANGUAGES C CXX)

include(CheckIPOSupported)
check_ipo_supported(RESULT BOX3D_IPO_SUPPORTED OUTPUT BOX3D_IPO_ERROR)
if(NOT BOX3D_IPO_SUPPORTED)
	message(FATAL_ERROR "Release IPO is required: ${BOX3D_IPO_ERROR}")
endif()

set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX3D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX3D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX3D_VALIDATE OFF CACHE BOOL "" FORCE)
set(BOX3D_REF_PATH "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Box3D source checkout")

add_subdirectory("${BOX3D_REF_PATH}" "${CMAKE_BINARY_DIR}/box3d")

file(GLOB BOX3D_CASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../cases/*_case.cpp")
add_executable(box3d_polygon_runner
	box3d_polygon_runner.cpp
	box3d_ray_tracing_results.cpp
	box3d_ray_tracing_recording.cpp
	box3d_result_writer.cpp
	box3d_runner_args.cpp
	box3d_visual_snapshot.cpp
	../cases/box3d_case_registry.cpp
	../cases/box3d_query_executor.cpp
	${BOX3D_CASE_SOURCES}
)
target_include_directories(box3d_polygon_runner PRIVATE
	.
	../cases
	../common
	../../rendering/include
)
target_link_libraries(box3d_polygon_runner PRIVATE box3d)
set_property(TARGET box3d box3d_polygon_runner
	PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
target_compile_features(box3d_polygon_runner PRIVATE cxx_std_17)
