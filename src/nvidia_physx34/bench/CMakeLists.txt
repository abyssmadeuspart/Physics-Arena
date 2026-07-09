cmake_minimum_required(VERSION 3.22)

project(nvidia_physx34_polygon_runner LANGUAGES C CXX)

if(NOT DEFINED NVIDIA_PHYSX34_ROOT)
	message(FATAL_ERROR "NVIDIA_PHYSX34_ROOT is required")
endif()
if(NOT DEFINED NVIDIA_PHYSX34_SDK_LIB_DIR)
	message(FATAL_ERROR "NVIDIA_PHYSX34_SDK_LIB_DIR is required")
endif()
if(NOT DEFINED NVIDIA_PHYSX34_PXSHARED_LIB_DIR)
	message(FATAL_ERROR "NVIDIA_PHYSX34_PXSHARED_LIB_DIR is required")
endif()
if(NOT DEFINED NVIDIA_PHYSX34_SDK_BIN_DIR)
	message(FATAL_ERROR "NVIDIA_PHYSX34_SDK_BIN_DIR is required")
endif()

function(require_nvidia_physx34_library output_name)
	find_library(
		${output_name}
		NAMES ${ARGN}
		PATHS "${NVIDIA_PHYSX34_SDK_LIB_DIR}" "${NVIDIA_PHYSX34_PXSHARED_LIB_DIR}"
		NO_DEFAULT_PATH)
	if(NOT ${output_name})
		message(FATAL_ERROR "Missing PhysX library ${output_name} in ${NVIDIA_PHYSX34_SDK_LIB_DIR}")
	endif()
endfunction()

require_nvidia_physx34_library(NVIDIA_PHYSX34_PHYSX_LIBRARY PhysX3_x64 PhysX3)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PHYSX_COMMON_LIBRARY PhysX3Common_x64 PhysX3Common)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PHYSX_COOKING_LIBRARY PhysX3Cooking_x64 PhysX3Cooking)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PHYSX_EXTENSIONS_LIBRARY PhysX3Extensions_x64 PhysX3Extensions)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PXFOUNDATION_LIBRARY PxFoundation_x64 PxFoundation)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PXPVDSDK_LIBRARY PxPvdSDK_x64 PxPvdSDK)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PXTASK_LIBRARY PxTask_x64 PxTask)
require_nvidia_physx34_library(NVIDIA_PHYSX34_PSFASTXML_LIBRARY PsFastXml_x64 PsFastXml)

add_executable(nvidia_physx34_polygon_runner
	nvidia_physx34_polygon_runner.cpp
	nvidia_physx34_result_writer.cpp
	nvidia_physx34_runner_args.cpp
	nvidia_physx34_shared_visual_mode.cpp
	../cases/nvidia_physx34_case_registry.cpp
	../cases/nvidia_physx34_box_container_pile_10k_case.cpp)
target_compile_features(nvidia_physx34_polygon_runner PRIVATE cxx_std_17)
target_compile_definitions(nvidia_physx34_polygon_runner PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(
	nvidia_physx34_polygon_runner
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}"
		"${CMAKE_CURRENT_LIST_DIR}/../cases"
		"${CMAKE_CURRENT_LIST_DIR}/../../rendering/include"
		"${NVIDIA_PHYSX34_ROOT}/PhysX_3.4/Include"
		"${NVIDIA_PHYSX34_ROOT}/PxShared/include")
target_link_libraries(
	nvidia_physx34_polygon_runner
	PRIVATE
		"${NVIDIA_PHYSX34_PHYSX_LIBRARY}"
		"${NVIDIA_PHYSX34_PHYSX_COMMON_LIBRARY}"
		"${NVIDIA_PHYSX34_PHYSX_COOKING_LIBRARY}"
		"${NVIDIA_PHYSX34_PHYSX_EXTENSIONS_LIBRARY}"
		"${NVIDIA_PHYSX34_PXFOUNDATION_LIBRARY}"
		"${NVIDIA_PHYSX34_PXPVDSDK_LIBRARY}"
		"${NVIDIA_PHYSX34_PXTASK_LIBRARY}"
		"${NVIDIA_PHYSX34_PSFASTXML_LIBRARY}")

if(WIN32)
	target_link_libraries(nvidia_physx34_polygon_runner PRIVATE ws2_32)
endif()

if(MSVC)
	set_property(TARGET nvidia_physx34_polygon_runner PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
