cmake_minimum_required(VERSION 3.40)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(ImGui)

find_package(Toaster CONFIG REQUIRED)

set(IMGUI_SRC
		"${CMAKE_CURRENT_SOURCE_DIR}/imgui.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/imgui_draw.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/imgui_tables.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/imgui_widgets.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/imgui_demo.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_toaster.cpp"
)

add_library(ImGui STATIC)

target_link_libraries(ImGui PUBLIC Toaster::ToastGPU glfw)

target_sources(ImGui PRIVATE ${IMGUI_SRC})
target_include_directories(ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

option(IMGUI_BUILD_TESTS "Build the one debug test" ON)

if (IMGUI_BUILD_TESTS)
	add_executable(ImGuiTest WIN32)

	target_link_libraries(ImGuiTest PRIVATE ImGui Toaster::ToastKernel Toaster::ToastOS Toaster::ToastAsset)
	target_sources(ImGuiTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/example_toaster/main.cpp")
	target_include_directories(ImGuiTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/example_toaster")
endif ()


