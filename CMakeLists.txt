cmake_minimum_required(VERSION 3.22)
project(dizui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force Clang + Ninja on Linux if not already set, but let user override
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Note: This project is optimized for Clang and Ninja.")
endif()

include(FetchContent)

# --- Fetch GLFW (required for ImGui backend) ---
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(glfw)

# --- Fetch ImGui (docking branch) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# --- Fetch ImGui Node Editor ---
FetchContent_Declare(
    imgui_node_editor
    GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(imgui_node_editor)

# --- Define Third-Party Library Targets (To Suppress Warnings) ---
# ImGui Core + OpenGL3/GLFW Backends
add_library(imgui_src STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_src SYSTEM PUBLIC 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_src PUBLIC glfw)
# Treat headers as SYSTEM to prevent warnings
target_compile_options(imgui_src PRIVATE -w)

# ImGui Node Editor
add_library(imgui_node_editor_src STATIC
    ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
)
target_include_directories(imgui_node_editor_src SYSTEM PUBLIC 
    ${imgui_node_editor_SOURCE_DIR}
)
target_link_libraries(imgui_node_editor_src PUBLIC imgui_src)
target_compile_options(imgui_node_editor_src PRIVATE -w)

# --- Main Project Executable ---
file(GLOB_RECURSE PROJECT_HEADERS "include/*.hpp")
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp")

add_executable(dizui ${PROJECT_SOURCES} ${PROJECT_HEADERS})

# --- NOUVEAU : Injecter le chemin absolu des ressources au compilateur ---
target_compile_definitions(dizui PRIVATE 
    DIZUI_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_include_directories(dizui PRIVATE include)
target_link_libraries(dizui PRIVATE imgui_node_editor_src GL)

# Strict compilation flags only for our project code
target_compile_options(dizui PRIVATE -Wall -Wextra -Wpedantic)

# Copier le dossier des assets (contenant la police d'icônes) dans le dossier de build
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_BINARY_DIR}")
