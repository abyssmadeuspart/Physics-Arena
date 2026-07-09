cmake_minimum_required(VERSION 3.22)

project(benchmark_polygon_shared_visual_renderer LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(SDL_REF_PATH "" CACHE PATH "SDL reference checkout")
set(BX_REF_PATH "" CACHE PATH "bx reference checkout")
set(BIMG_REF_PATH "" CACHE PATH "bimg reference checkout")
set(BGFX_REF_PATH "" CACHE PATH "bgfx reference checkout")
set(IMGUI_REF_PATH "" CACHE PATH "Dear ImGui reference checkout")
set(IMPLOT_REF_PATH "" CACHE PATH "ImPlot reference checkout")
set(BENCHMARK_VISUAL_BUILD_APP ON CACHE BOOL "Build the standalone shared visual renderer executable")

find_package(Git REQUIRED)

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

function(render_git_commit out_var root)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${root}" rev-parse HEAD
		OUTPUT_VARIABLE commit
		ERROR_VARIABLE git_error
		RESULT_VARIABLE git_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(NOT git_result EQUAL 0)
		message(FATAL_ERROR "git rev-parse failed for ${root}: ${git_error}")
	endif()
	set("${out_var}" "${commit}" PARENT_SCOPE)
endfunction()

render_require_path(SDL "${SDL_REF_PATH}")
render_require_path(BX "${BX_REF_PATH}")
render_require_path(BIMG "${BIMG_REF_PATH}")
render_require_path(BGFX "${BGFX_REF_PATH}")
render_require_path(IMGUI "${IMGUI_REF_PATH}")
render_require_path(IMPLOT "${IMPLOT_REF_PATH}")

render_require_file(SDL "${SDL_REF_PATH}" "CMakeLists.txt")
render_require_file(SDL "${SDL_REF_PATH}" "include/SDL3/SDL.h")
render_require_file(BX "${BX_REF_PATH}" "include/bx/bx.h")
render_require_file(BIMG "${BIMG_REF_PATH}" "include/bimg/bimg.h")
render_require_file(BGFX "${BGFX_REF_PATH}" "include/bgfx/bgfx.h")
render_require_file(BGFX "${BGFX_REF_PATH}" "src/amalgamated.cpp")
render_require_file(IMGUI "${IMGUI_REF_PATH}" "imgui.cpp")
render_require_file(IMGUI "${IMGUI_REF_PATH}" "backends/imgui_impl_sdl3.cpp")
render_require_file(IMPLOT "${IMPLOT_REF_PATH}" "implot.cpp")
render_require_file(IMPLOT "${IMPLOT_REF_PATH}" "implot_items.cpp")

render_git_commit(SDL_COMMIT "${SDL_REF_PATH}")
render_git_commit(BX_COMMIT "${BX_REF_PATH}")
render_git_commit(BIMG_COMMIT "${BIMG_REF_PATH}")
render_git_commit(BGFX_COMMIT "${BGFX_REF_PATH}")
render_git_commit(IMGUI_COMMIT "${IMGUI_REF_PATH}")
render_git_commit(IMPLOT_COMMIT "${IMPLOT_REF_PATH}")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_REF_PATH}" "${CMAKE_BINARY_DIR}/_deps/sdl" EXCLUDE_FROM_ALL)

function(render_apply_bx_toolchain target_name)
	target_compile_definitions("${target_name}" PRIVATE
		BX_CONFIG_DEBUG=0
		_HAS_EXCEPTIONS=0
		_SCL_SECURE=0
		_SECURE_SCL=0
		_SCL_SECURE_NO_WARNINGS
		_CRT_SECURE_NO_WARNINGS
		_CRT_SECURE_NO_DEPRECATE
		NOMINMAX
	)

	if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
		target_compile_options("${target_name}" PRIVATE /arch:AVX)
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64|i[3-6]86")
		target_compile_options("${target_name}" PRIVATE -msse4.2)
	endif()
endfunction()

file(GLOB BX_SOURCES CONFIGURE_DEPENDS
	"${BX_REF_PATH}/src/*.cpp"
)
list(REMOVE_ITEM BX_SOURCES "${BX_REF_PATH}/src/amalgamated.cpp")

add_library(bx STATIC
	${BX_SOURCES}
)

target_include_directories(bx PUBLIC
	"${BX_REF_PATH}/include"
	"${BX_REF_PATH}/3rdparty"
	"${BX_REF_PATH}/include/compat/msvc"
)

target_compile_features(bx PRIVATE cxx_std_20)
render_apply_bx_toolchain(bx)

file(GLOB BIMG_ASTC_SOURCES CONFIGURE_DEPENDS
	"${BIMG_REF_PATH}/3rdparty/astc-encoder/source/*.cpp"
)

add_library(bimg STATIC
	"${BIMG_REF_PATH}/src/image.cpp"
	${BIMG_ASTC_SOURCES}
)

target_include_directories(bimg PUBLIC
	"${BIMG_REF_PATH}/include"
	"${BIMG_REF_PATH}/src"
	"${BIMG_REF_PATH}/3rdparty/astc-encoder/include"
)

target_compile_definitions(bimg PRIVATE
	ASTCENC_F16C=0
	ASTCENC_NEON=0
)
target_compile_features(bimg PRIVATE cxx_std_20)
target_link_libraries(bimg PUBLIC bx)
render_apply_bx_toolchain(bimg)

add_library(bgfx STATIC
	"${BGFX_REF_PATH}/src/amalgamated.cpp"
)

target_include_directories(bgfx PUBLIC
	"${BGFX_REF_PATH}/include"
	"${BGFX_REF_PATH}/3rdparty"
	"${BGFX_REF_PATH}/3rdparty/khronos"
	"${BX_REF_PATH}/include"
	"${BIMG_REF_PATH}/include"
	"${BIMG_REF_PATH}/3rdparty"
)

target_include_directories(bgfx PRIVATE
	"${BGFX_REF_PATH}/src"
)

target_compile_definitions(bgfx PRIVATE
	BGFX_CONFIG_RENDERER_DIRECT3D11=1
	BGFX_CONFIG_RENDERER_DIRECT3D12=0
	BGFX_CONFIG_RENDERER_OPENGL=0
	BGFX_CONFIG_RENDERER_OPENGLES=0
	BGFX_CONFIG_RENDERER_VULKAN=0
	BGFX_CONFIG_RENDERER_WEBGPU=0
)
target_compile_features(bgfx PRIVATE cxx_std_20)
target_link_libraries(bgfx PUBLIC bx bimg)
render_apply_bx_toolchain(bgfx)

if(WIN32)
	target_link_libraries(bgfx PUBLIC d3d11 dxgi dxguid d3dcompiler)
endif()

add_library(imgui STATIC
	"${IMGUI_REF_PATH}/imgui.cpp"
	"${IMGUI_REF_PATH}/imgui_draw.cpp"
	"${IMGUI_REF_PATH}/imgui_tables.cpp"
	"${IMGUI_REF_PATH}/imgui_widgets.cpp"
	"${IMGUI_REF_PATH}/backends/imgui_impl_sdl3.cpp"
)

target_include_directories(imgui PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}/include"
	"${IMGUI_REF_PATH}"
	"${IMGUI_REF_PATH}/backends"
)

target_compile_definitions(imgui PUBLIC
	IMGUI_USER_CONFIG="benchmark_visual/imconfig.h"
)
target_compile_features(imgui PRIVATE cxx_std_17)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(implot STATIC
	"${IMPLOT_REF_PATH}/implot.cpp"
	"${IMPLOT_REF_PATH}/implot_items.cpp"
)

target_include_directories(implot PUBLIC
	"${IMPLOT_REF_PATH}"
)

target_compile_features(implot PRIVATE cxx_std_17)
target_link_libraries(implot PUBLIC imgui)

file(WRITE "${CMAKE_BINARY_DIR}/cmake-dependency-provenance.json"
"{\n  \"schema_version\": 1,\n  \"configure_status\": \"ok\",\n  \"dependencies\": [\n    {\"id\": \"sdl\", \"path\": \"${SDL_REF_PATH}\", \"commit\": \"${SDL_COMMIT}\"},\n    {\"id\": \"bx\", \"path\": \"${BX_REF_PATH}\", \"commit\": \"${BX_COMMIT}\"},\n    {\"id\": \"bimg\", \"path\": \"${BIMG_REF_PATH}\", \"commit\": \"${BIMG_COMMIT}\"},\n    {\"id\": \"bgfx\", \"path\": \"${BGFX_REF_PATH}\", \"commit\": \"${BGFX_COMMIT}\"},\n    {\"id\": \"imgui\", \"path\": \"${IMGUI_REF_PATH}\", \"commit\": \"${IMGUI_COMMIT}\"},\n    {\"id\": \"implot\", \"path\": \"${IMPLOT_REF_PATH}\", \"commit\": \"${IMPLOT_COMMIT}\"}\n  ]\n}\n")

add_custom_target(benchmark_visual_dependency_probe ALL
	COMMAND "${CMAKE_COMMAND}" -E echo "render viewer dependency provenance written"
)

add_library(benchmark_visual_runtime STATIC
	src/live_snapshot.cpp
	src/platform_sdl3.cpp
	src/renderer_bgfx.cpp
	src/imgui_bgfx_renderer.cpp
	src/physics_scene_view.cpp
	src/ui_live_metrics.cpp
	src/result_data.cpp
)

target_include_directories(benchmark_visual_runtime PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}/include"
)

target_include_directories(benchmark_visual_runtime PRIVATE
	"${IMGUI_REF_PATH}"
	"${IMGUI_REF_PATH}/backends"
	"${BGFX_REF_PATH}/examples/common/debugdraw"
	"${BGFX_REF_PATH}/examples/common/imgui"
)

target_compile_definitions(benchmark_visual_runtime PUBLIC SDL_MAIN_HANDLED)
target_compile_features(benchmark_visual_runtime PRIVATE cxx_std_20)
target_link_libraries(benchmark_visual_runtime PUBLIC SDL3::SDL3 bgfx imgui implot)

if(BENCHMARK_VISUAL_BUILD_APP)
	add_executable(shared_visual_renderer_cli
		src/main.cpp
	)

	target_compile_features(shared_visual_renderer_cli PRIVATE cxx_std_17)
	target_link_libraries(shared_visual_renderer_cli PRIVATE benchmark_visual_runtime)
	if(WIN32)
		target_link_libraries(shared_visual_renderer_cli PRIVATE ws2_32)
	endif()
	set_target_properties(shared_visual_renderer_cli PROPERTIES OUTPUT_NAME "shared_visual_renderer_cli")
endif()
