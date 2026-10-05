cmake_minimum_required(VERSION 3.22)

set(PHYSICS_ARENA_JSON_REF_PATH "" CACHE PATH "Pinned nlohmann/json checkout")
set(PHYSICS_ARENA_CSV_PARSER_REF_PATH "" CACHE PATH "Pinned CSV Parser checkout")

find_package(Git REQUIRED)

function(physics_arena_require_reference name root expected_commit required_file)
	if("${root}" STREQUAL "")
		message(FATAL_ERROR "${name} reference path is required")
	endif()
	if(NOT EXISTS "${root}/${required_file}")
		message(FATAL_ERROR "${name} missing ${required_file}: ${root}")
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${root}" rev-parse HEAD
		OUTPUT_VARIABLE actual_commit
		ERROR_VARIABLE commit_error
		RESULT_VARIABLE commit_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(NOT commit_result EQUAL 0)
		message(FATAL_ERROR "${name} commit probe failed: ${commit_error}")
	endif()
	if(NOT actual_commit STREQUAL expected_commit)
		message(FATAL_ERROR "${name} commit ${actual_commit}; expected ${expected_commit}")
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${root}" status --porcelain=v1 --untracked-files=normal
		OUTPUT_VARIABLE dirty_paths
		ERROR_VARIABLE status_error
		RESULT_VARIABLE status_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(NOT status_result EQUAL 0)
		message(FATAL_ERROR "${name} cleanliness probe failed: ${status_error}")
	endif()
	if(NOT dirty_paths STREQUAL "")
		message(FATAL_ERROR "${name} reference is dirty: ${root}")
	endif()

endfunction()

physics_arena_require_reference(
	"nlohmann-json" "${PHYSICS_ARENA_JSON_REF_PATH}"
	"55f93686c01528224f448c19128836e7df245f72" "include/nlohmann/json.hpp"
)
physics_arena_require_reference(
	"csv-parser" "${PHYSICS_ARENA_CSV_PARSER_REF_PATH}"
	"32e99be14236b0585f33fb96f37e4fefc363f448" "include/csv.hpp"
)

set(PHYSICS_ARENA_ZSTD_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.build/dependencies/zstd-1.5.7")
physics_arena_require_reference(
	"zstd" "${PHYSICS_ARENA_ZSTD_PATH}"
	"f8745da6ff1ad1e7bab384bd1f9d742439278e99" "contrib/seekable_format/zstd_seekable.h"
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_USE_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
add_subdirectory("${PHYSICS_ARENA_ZSTD_PATH}/build/cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/third_party/zstd" EXCLUDE_FROM_ALL)
add_library(physics_arena_zstd_seekable STATIC
	"${PHYSICS_ARENA_ZSTD_PATH}/contrib/seekable_format/zstdseek_compress.c"
	"${PHYSICS_ARENA_ZSTD_PATH}/contrib/seekable_format/zstdseek_decompress.c"
)
target_include_directories(physics_arena_zstd_seekable PUBLIC
	"${PHYSICS_ARENA_ZSTD_PATH}/contrib/seekable_format"
	"${PHYSICS_ARENA_ZSTD_PATH}/lib")
target_include_directories(physics_arena_zstd_seekable PRIVATE "${PHYSICS_ARENA_ZSTD_PATH}/lib/common")
target_compile_definitions(physics_arena_zstd_seekable PRIVATE XXH_NAMESPACE=ZSTD_)
target_link_libraries(physics_arena_zstd_seekable PUBLIC libzstd_static)

set(BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(CSV_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(CSV_BUILD_SINGLE_INCLUDE_TEST OFF CACHE BOOL "" FORCE)
set(CSV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CSV_ENABLE_THREADS OFF CACHE BOOL "" FORCE)
add_subdirectory(
	"${PHYSICS_ARENA_CSV_PARSER_REF_PATH}"
	"${CMAKE_CURRENT_BINARY_DIR}/third_party/csv-parser"
	EXCLUDE_FROM_ALL
)

add_library(physics_arena_core STATIC
	src/bench_types.cpp
	src/case_execution.cpp
	src/stack_stability.cpp
	src/stack_verification_channel.cpp
	src/contact_islands_stability.cpp
	src/stack_stability_reassessment.cpp
	src/ray_tracing_corpus.cpp
	src/ray_tracing_preflight.cpp
	src/ray_tracing_corpus_cache.cpp
	src/ray_tracing_reference.cpp
	src/ray_tracing_results.cpp
	src/ray_tracing_images.cpp
	src/csv_io.cpp
	src/host_windows.cpp
	src/invocation.cpp
	src/json_contracts.cpp
	src/json_case_contracts.cpp
	src/json_engine_contracts.cpp
	src/json_report_contracts.cpp
	src/json_release_artifacts.cpp
	src/json_result_manifest.cpp
	src/json_visual_adapter.cpp
	src/observation_results.cpp
	src/process_windows.cpp
	src/report_engine_logo.cpp
	src/report_pipeline.cpp
	src/report_outcomes.cpp
	src/report_outcome_presentation.cpp
	src/report_markdown.cpp
	src/report_summary_svg.cpp
	src/report_timing_svg.cpp
	src/release_contracts.cpp
	src/result_pipeline.cpp
	src/result_projection.cpp
	src/result_repeat_projection.cpp
	src/result_view_model.cpp
	src/run.cpp
	src/run_recommendations.cpp
	src/run_repeat_policy.cpp
	src/run_settings.cpp
	src/replay.cpp
	src/replay_comparison.cpp
	src/replay_compression.cpp
	src/replay_storage.cpp
	../rendering/src/live_snapshot.cpp
	../rendering/src/visual_camera.cpp
	../rendering/src/replay_camera.cpp
	src/run_manifest.cpp
	src/run_execution.cpp
	src/run_finalization.cpp
	src/run_recovery.cpp
	src/timing_series.cpp
)
target_compile_features(physics_arena_core PUBLIC cxx_std_20)
target_include_directories(physics_arena_core
	PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
	PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../common"
	PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../rendering/include"
	PRIVATE "${PHYSICS_ARENA_JSON_REF_PATH}/include"
)
target_link_libraries(physics_arena_core PRIVATE csv physics_arena_zstd_seekable)
if(WIN32)
	target_link_libraries(physics_arena_core PRIVATE ole32 oleaut32 wbemuuid)
endif()

if(MSVC)
	target_compile_options(physics_arena_core PRIVATE /W4 /WX /permissive- /GR- /EHs-c- /Os /Ob1 /clang:-Oz)
	set_source_files_properties(
		src/json_contracts.cpp
		src/json_case_contracts.cpp
		src/run_settings.cpp
		src/json_engine_contracts.cpp
		src/json_report_contracts.cpp
		src/json_release_artifacts.cpp
		src/json_result_manifest.cpp
		src/json_visual_adapter.cpp
		src/csv_io.cpp
		PROPERTIES COMPILE_OPTIONS "/EHsc"
	)
endif()
