cmake_minimum_required(VERSION 3.22)

project(box3d_polygon_runner LANGUAGES C CXX)

set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX3D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX3D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX3D_VALIDATE OFF CACHE BOOL "" FORCE)
set(BOX3D_REF_PATH "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Box3D source checkout")

add_subdirectory("${BOX3D_REF_PATH}" "${CMAKE_BINARY_DIR}/box3d")

add_executable(box3d_polygon_runner
	box3d_polygon_runner.cpp
	box3d_result_writer.cpp
	box3d_runner_args.cpp
	box3d_shared_visual_mode.cpp
	box3d_visual_cli.cpp
	box3d_visual_snapshot.cpp
	box3d_visual_transport.cpp
	../cases/box3d_case_registry.cpp
	../cases/box3d_box_container_pile_10k_case.cpp
)
target_include_directories(box3d_polygon_runner PRIVATE
	.
	../cases
	../../rendering/include
)
target_link_libraries(box3d_polygon_runner PRIVATE box3d)
if(WIN32)
	target_link_libraries(box3d_polygon_runner PRIVATE ws2_32)
endif()
target_compile_features(box3d_polygon_runner PRIVATE cxx_std_17)
