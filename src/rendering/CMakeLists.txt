cmake_minimum_required(VERSION 3.22)

project(benchmark_polygon_physics_arena LANGUAGES C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(RAYLIB_REF_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.build/dependencies/raylib-6.0_win64_msvc16" CACHE PATH "raylib 6.0 static MSVC archive")
set(RLIMGUI_REF_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.build/dependencies/rlImGui" CACHE PATH "rlImGui reference checkout")
set(IMGUI_REF_PATH "" CACHE PATH "Dear ImGui reference checkout")
set(IMPLOT_REF_PATH "" CACHE PATH "ImPlot reference checkout")
set(BENCHMARK_VISUAL_BUILD_APP ON CACHE BOOL "Build the standalone shared visual renderer executable")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../physics_arena" "${CMAKE_BINARY_DIR}/physics_arena")

if(WIN32)
	enable_language(RC)
endif()

function(render_require_path name root)
	if("${root}" STREQUAL "")
		message(FATAL_ERROR "${name}_REF_PATH is required")
	endif()
	if(NOT EXISTS "${root}")
		message(FATAL_ERROR "${name}_REF_PATH does not exist: ${root}")
	endif()
endfunction()

function(render_require_file name root relative_path)
	if(NOT EXISTS "${root}/${relative_path}")
		message(FATAL_ERROR "${name} missing required file: ${relative_path}")
	endif()
endfunction()

render_require_path(RAYLIB "${RAYLIB_REF_PATH}")
render_require_path(RLIMGUI "${RLIMGUI_REF_PATH}")
render_require_path(IMGUI "${IMGUI_REF_PATH}")
render_require_path(IMPLOT "${IMPLOT_REF_PATH}")
render_require_file(RAYLIB "${RAYLIB_REF_PATH}" "include/raylib.h")
render_require_file(RAYLIB "${RAYLIB_REF_PATH}" "lib/raylib.lib")
render_require_file(RLIMGUI "${RLIMGUI_REF_PATH}" "rlImGui.cpp")
render_require_file(RLIMGUI "${RLIMGUI_REF_PATH}" "imgui_impl_raylib.h")
render_require_file(IMGUI "${IMGUI_REF_PATH}" "imgui.cpp")
render_require_file(IMPLOT "${IMPLOT_REF_PATH}" "implot.cpp")
add_library(raylib STATIC IMPORTED)
set_target_properties(raylib PROPERTIES
	IMPORTED_LOCATION "${RAYLIB_REF_PATH}/lib/raylib.lib"
	INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_REF_PATH}/include"
	INTERFACE_LINK_LIBRARIES "winmm;gdi32;opengl32;shell32"
)

add_library(imgui STATIC
	"${IMGUI_REF_PATH}/imgui.cpp"
	"${IMGUI_REF_PATH}/imgui_draw.cpp"
	"${IMGUI_REF_PATH}/imgui_tables.cpp"
	"${IMGUI_REF_PATH}/imgui_widgets.cpp"
	"${IMGUI_REF_PATH}/backends/imgui_impl_win32.cpp"
)

target_include_directories(imgui PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}/include"
	"${IMGUI_REF_PATH}"
)

target_compile_definitions(imgui PUBLIC
	IMGUI_USER_CONFIG="benchmark_visual/imconfig.h"
)
target_compile_features(imgui PRIVATE cxx_std_17)
add_library(rlimgui STATIC "${RLIMGUI_REF_PATH}/rlImGui.cpp")
target_include_directories(rlimgui PUBLIC "${RLIMGUI_REF_PATH}")
target_compile_definitions(rlimgui PRIVATE NO_FONT_AWESOME)
target_compile_features(rlimgui PRIVATE cxx_std_20)
target_link_libraries(rlimgui PUBLIC raylib imgui)

add_library(implot STATIC
	"${IMPLOT_REF_PATH}/implot.cpp"
	"${IMPLOT_REF_PATH}/implot_items.cpp"
)

target_include_directories(implot PUBLIC
	"${IMPLOT_REF_PATH}"
)

target_compile_features(implot PRIVATE cxx_std_17)
target_link_libraries(implot PUBLIC imgui)

add_library(benchmark_visual_runtime STATIC
	src/platform_raylib.cpp
	src/renderer_raylib.cpp
	src/imgui_raylib_renderer.cpp
	src/physics_scene_view.cpp
)

target_include_directories(benchmark_visual_runtime PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}/include"
)

target_include_directories(benchmark_visual_runtime PRIVATE
	"${IMGUI_REF_PATH}"
)

target_compile_features(benchmark_visual_runtime PRIVATE cxx_std_20)
target_link_libraries(benchmark_visual_runtime PUBLIC raylib rlimgui imgui implot comctl32)
if(MSVC)
	target_compile_options(benchmark_visual_runtime PRIVATE /W4 /WX /permissive-)
endif()

if(BENCHMARK_VISUAL_BUILD_APP)
	if(NOT WIN32)
		message(FATAL_ERROR "Physics Arena is a Windows x64 GUI application")
	endif()
	add_executable(PhysicsArena WIN32
		src/main.cpp
		src/native_app_model.cpp
		src/native_run_queue.cpp
		src/native_run_preferences.cpp
		src/native_run_presets.cpp
		src/native_value_format.cpp
		src/native_run_file_io.cpp
		src/launcher_run_presets.cpp
		src/native_action.cpp
		src/native_observability_model.cpp
		src/launcher_app.cpp
		src/launcher_workspace.cpp
		src/launcher_case_explanation.cpp
		src/launcher_result_inspector.cpp
		src/launcher_result_repeats.cpp
		src/launcher_recordings.cpp
		src/launcher_controls.cpp
		src/launcher_run_queue.cpp
		src/launcher_settings.cpp
		src/replay_view.cpp
		src/replay_comparison.cpp
		src/replay_comparison_view.cpp
		src/ray_tracing_view.cpp
		src/launcher_result_state.cpp
		src/launcher_results.cpp
		src/launcher_result_summary.cpp
		src/launcher_result_case_data.cpp
		src/launcher_result_timing.cpp
		src/launcher_observability.cpp
		physics_arena.rc
	)

	target_compile_features(PhysicsArena PRIVATE cxx_std_20)
	target_include_directories(PhysicsArena PRIVATE
		"${IMGUI_REF_PATH}"
		"${PHYSICS_ARENA_JSON_REF_PATH}/include"
	)
	target_link_libraries(PhysicsArena PRIVATE benchmark_visual_runtime physics_arena_core)
	if(WIN32)
		target_link_libraries(PhysicsArena PRIVATE shell32 ole32 comdlg32)
	endif()
	set_target_properties(PhysicsArena PROPERTIES OUTPUT_NAME "PhysicsArena")
	if(MSVC)
		set_source_files_properties(src/native_run_preferences.cpp src/native_run_presets.cpp PROPERTIES COMPILE_OPTIONS "/EHsc")
		target_compile_options(PhysicsArena PRIVATE /W4 /WX /permissive- /Os /Ob1 /clang:-Oz)
		target_link_options(PhysicsArena PRIVATE /STACK:8388608 /OPT:REF /OPT:ICF)
	endif()
endif()
