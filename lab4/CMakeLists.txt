cmake_minimum_required(VERSION 3.20)
include(FetchContent)

project(vulkan-starter-app LANGUAGES C CXX)

add_executable(${PROJECT_NAME} source/main.cpp source/graphics_internal.cpp source/graphics.cpp source/application.cpp)

if(LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(WIN32)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
		target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /entry:mainCRTStartup)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
		# Debug keeps the console to show validation layer messages.
		# Static runtime avoids loading incompatible libstdc++-6.dll from PATH (e.g. the one from Git)
		target_link_options(${PROJECT_NAME} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-mwindows> -static)
	endif()
	target_compile_definitions(${PROJECT_NAME} PRIVATE -D_USE_MATH_DEFINES)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.5.1
)

FetchContent_Declare(
	vk-bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
	GIT_TAG v1.4.321
)

FetchContent_Declare(
	VulkanMemoryAllocator
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.4.0
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.92.9
)

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.3
)

FetchContent_Declare(
	lodepng
	GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
	GIT_TAG 7b2893e35891c0c85bee5595caafaf3c2044b409
)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

set(GLM_BUILD_LIBRARY OFF) # Header-only
set(GLM_BUILD_TESTS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
	glm
	lodepng
)

if(MSVC)
	set(LAB1_WARNING_OPTIONS /W4)
else()
	set(LAB1_WARNING_OPTIONS -Wall -Wextra -Wno-missing-field-initializers)
endif()

# Pure C++ part of the lab (no Vulkan), shared by the application and unit tests
add_library(lab1-core STATIC source/options.cpp source/geometry.cpp source/scene.cpp)
target_include_directories(lab1-core PUBLIC source)
target_link_libraries(lab1-core PUBLIC glm::glm)
target_compile_options(lab1-core PRIVATE ${LAB1_WARNING_OPTIONS})
set_target_properties(lab1-core PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
	lab1-core
)

# Unit tests: build-*/lab1-tests.exe
option(LAB1_BUILD_TESTS "Build unit tests" ON)
if(LAB1_BUILD_TESTS)
	FetchContent_Declare(
		doctest
		GIT_REPOSITORY https://github.com/doctest/doctest.git
		GIT_TAG v2.4.12
	)
	set(DOCTEST_WITH_TESTS OFF)
	set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF)
	FetchContent_MakeAvailable(doctest)

	add_executable(lab1-tests
		tests/unit/main.cpp
		tests/unit/test_options.cpp
		tests/unit/test_geometry.cpp
		tests/unit/test_scene.cpp
	)
	target_link_libraries(lab1-tests PRIVATE lab1-core doctest::doctest)
	target_compile_options(lab1-tests PRIVATE ${LAB1_WARNING_OPTIONS})
	set_target_properties(lab1-tests PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)
	if(MINGW)
		target_link_options(lab1-tests PRIVATE -static)
	endif()

	enable_testing()
	add_test(NAME unit COMMAND lab1-tests)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${lodepng_SOURCE_DIR})
target_sources(${PROJECT_NAME} PRIVATE
	${lodepng_SOURCE_DIR}/lodepng.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
)

# Compile shaders
find_program(GLSLC_FOUND glslc)
if(GLSLC_FOUND)
	set(_SHADER_BINARIES)

	macro(compile_shader SHADER_FILE)
		set(SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE})
		set(SHADER_BINARY ${SHADER_FILE}.spv)
		set(SHADER_BINARY_PATH ${CMAKE_SOURCE_DIR}/shaders/${SHADER_BINARY})

		# Relative paths keep glslc error messages readable (and clickable)
		# when project path contains non-ASCII characters
		add_custom_command(
			OUTPUT ${SHADER_BINARY_PATH}
			COMMAND ${GLSLC_FOUND} shaders/${SHADER_FILE} -o shaders/${SHADER_BINARY}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			# Shaders include uniforms.glsl
			DEPENDS ${SHADER_SOURCE} ${CMAKE_SOURCE_DIR}/shaders/uniforms.glsl
			COMMENT "Compiling ${SHADER_FILE} shader"
		)

		list(APPEND _SHADER_BINARIES ${SHADER_BINARY_PATH})
	endmacro()

	# To compile shader file, use compile_shader function with a file name
	# of a shader inside shaders directory. See example below:

	# compile_shader(example.vert)
	# compile_shader(example.frag)

	compile_shader(shadow.vert)
	compile_shader(object.vert)
	compile_shader(object.frag)
	compile_shader(light_marker.vert)
	compile_shader(light_marker.frag)

	add_custom_target(shaders DEPENDS ${_SHADER_BINARIES})
	add_dependencies(${PROJECT_NAME} shaders)
endif()