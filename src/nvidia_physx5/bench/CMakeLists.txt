cmake_minimum_required(VERSION 3.22)

project(nvidia_physx5_polygon_runner LANGUAGES C CXX)

if(NOT DEFINED NVIDIA_PHYSX5_ROOT)
	message(FATAL_ERROR "NVIDIA_PHYSX5_ROOT is required")
endif()
if(NOT DEFINED NVIDIA_PHYSX5_SDK_LIB_DIR)
	message(FATAL_ERROR "NVIDIA_PHYSX5_SDK_LIB_DIR is required")
endif()
if(NOT DEFINED NVIDIA_PHYSX5_SDK_BIN_DIR)
	message(FATAL_ERROR "NVIDIA_PHYSX5_SDK_BIN_DIR is required")
endif()

function(require_nvidia_physx5_library output_name)
	find_library(
		${output_name}
		NAMES ${ARGN}
		PATHS "${NVIDIA_PHYSX5_SDK_LIB_DIR}"
		NO_DEFAULT_PATH)
	if(NOT ${output_name})
		message(FATAL_ERROR "Missing PhysX library ${output_name} in ${NVIDIA_PHYSX5_SDK_LIB_DIR}")
	endif()
endfunction()

require_nvidia_physx5_library(NVIDIA_PHYSX5_PHYSX_LIBRARY PhysX_64 PhysX)
require_nvidia_physx5_library(NVIDIA_PHYSX5_PHYSX_COMMON_LIBRARY PhysXCommon_64 PhysXCommon)
require_nvidia_physx5_library(NVIDIA_PHYSX5_PHYSX_COOKING_LIBRARY PhysXCooking_64 PhysXCooking)
require_nvidia_physx5_library(NVIDIA_PHYSX5_PHYSX_EXTENSIONS_LIBRARY PhysXExtensions_static_64 PhysXExtensions_static PhysXExtensions)
require_nvidia_physx5_library(NVIDIA_PHYSX5_PXFOUNDATION_LIBRARY PhysXFoundation_64 PhysXFoundation)
require_nvidia_physx5_library(NVIDIA_PHYSX5_PXPVDSDK_LIBRARY PhysXPvdSDK_static_64 PhysXPvdSDK_static PhysXPvdSDK)

file(GLOB NVIDIA_PHYSX5_CASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../cases/*_case.cpp")
add_executable(nvidia_physx5_polygon_runner
	nvidia_physx5_polygon_runner.cpp
	nvidia_physx5_result_writer.cpp
	nvidia_physx5_runner_args.cpp
	../cases/nvidia_physx5_case_registry.cpp
	${NVIDIA_PHYSX5_CASE_SOURCES})
target_compile_features(nvidia_physx5_polygon_runner PRIVATE cxx_std_17)
target_compile_definitions(nvidia_physx5_polygon_runner PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(
	nvidia_physx5_polygon_runner
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}"
		"${CMAKE_CURRENT_LIST_DIR}/../cases"
		"${CMAKE_CURRENT_LIST_DIR}/../../common"
		"${CMAKE_CURRENT_LIST_DIR}/../../rendering/include"
		"${NVIDIA_PHYSX5_ROOT}/include")
target_link_libraries(
	nvidia_physx5_polygon_runner
	PRIVATE
		"${NVIDIA_PHYSX5_PHYSX_LIBRARY}"
		"${NVIDIA_PHYSX5_PHYSX_COMMON_LIBRARY}"
		"${NVIDIA_PHYSX5_PHYSX_COOKING_LIBRARY}"
		"${NVIDIA_PHYSX5_PHYSX_EXTENSIONS_LIBRARY}"
		"${NVIDIA_PHYSX5_PXFOUNDATION_LIBRARY}"
		"${NVIDIA_PHYSX5_PXPVDSDK_LIBRARY}")


if(MSVC)
	set_property(TARGET nvidia_physx5_polygon_runner PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
