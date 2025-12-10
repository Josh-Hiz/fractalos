cmake_minimum_required(VERSION 3.10)
project(mandelbulb_gpu)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 3.3 REQUIRED)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(imgui PUBLIC glfw)

add_executable(mandelbulb
    src/main.cpp
)

target_include_directories(mandelbulb PRIVATE
    ${OPENGL_INCLUDE_DIR}
)

target_link_libraries(mandelbulb PRIVATE
    OpenGL::GL
    GLEW::GLEW
    glfw      # same GLFW target as above
    imgui
)

target_compile_definitions(mandelbulb PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLEW)
