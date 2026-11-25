cmake_minimum_required(VERSION 3.22)
project(dizui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Note: This project is optimized for Clang and Ninja.")
endif()

# ============================================================================
# SUPPORT HOMEBREW SUR MACOS (Apple Silicon & Intel)
# ============================================================================
if(APPLE)
    if(EXISTS "/opt/homebrew")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
        include_directories(SYSTEM "/opt/homebrew/include")
        link_directories("/opt/homebrew/lib")
    elseif(EXISTS "/usr/local")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local")
        include_directories(SYSTEM "/usr/local/include")
        link_directories("/usr/local/lib")
    endif()
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

# --- Define Third-Party Library Targets ---
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
target_compile_options(imgui_src PRIVATE -w)

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
target_compile_options(imgui_node_editor_src PRIVATE -w -include exception)

# --- Main Project Executable ---
file(GLOB_RECURSE PROJECT_HEADERS "include/*.hpp")
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp")

add_executable(dizui ${PROJECT_SOURCES} ${PROJECT_HEADERS})

target_compile_definitions(dizui PRIVATE 
    DIZUI_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_include_directories(dizui PRIVATE include)

# Détection multiplateforme d'OpenGL (Linux: GL / macOS: OpenGL::GL + Cocoa/IOKit)
find_package(OpenGL REQUIRED)
target_link_libraries(dizui PRIVATE imgui_node_editor_src OpenGL::GL)

if(APPLE)
    target_link_libraries(dizui PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

target_compile_options(dizui PRIVATE -Wall -Wextra -Wpedantic)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_BINARY_DIR}")

# ============================================================================
# LIAISON AVEC LE BACKEND CPPSQLX
# ============================================================================
if(APPLE)
    if(EXISTS "/opt/homebrew/opt/libpq")
        set(PostgreSQL_ROOT "/opt/homebrew/opt/libpq")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/opt/homebrew/opt/libpq")
    elseif(EXISTS "/usr/local/opt/libpq")
        set(PostgreSQL_ROOT "/usr/local/opt/libpq")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/usr/local/opt/libpq")
    endif()
endif()

find_package(PostgreSQL REQUIRED)

if(NOT DEFINED CPPSQLX_DIR)
    if(EXISTS "/home/santili/cppsqlx/cppsqlx")
        set(CPPSQLX_DIR "/home/santili/cppsqlx/cppsqlx" CACHE PATH "Chemin vers cppsqlx")
    else()
        set(CPPSQLX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cppsqlx" CACHE PATH "Chemin vers cppsqlx")
    endif()
endif()

target_include_directories(dizui PRIVATE 
    ${CPPSQLX_DIR}/include
    ${CPPSQLX_DIR}/build/_deps/json-src/include
    ${PostgreSQL_INCLUDE_DIRS}
)

find_library(CPPSQLX_LIB
    NAMES cppsqlx
    PATHS ${CPPSQLX_DIR}/build/src
    REQUIRED
)

find_library(LOG4CXX_LIB
    NAMES log4cxx
    REQUIRED
)

find_package(CURL REQUIRED)

target_link_libraries(dizui PRIVATE 
    ${CPPSQLX_LIB}
    ${LOG4CXX_LIB}
    PostgreSQL::PostgreSQL
    CURL::libcurl
)

set_target_properties(dizui PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${CPPSQLX_DIR}/build/src"
)


add_custom_command(TARGET dizui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:dizui>/assets"
)
