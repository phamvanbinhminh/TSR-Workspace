cmake_minimum_required(VERSION 3.10)
project(SEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Đảm bảo MSVC đọc source file và string literal là UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# LuaJIT có thể dùng nhưng hơi khó build, nên tạm dùng Lua 5.4
find_package(unofficial-lua REQUIRED)

# ===== ENGINE DLL =====
add_library(SEngine SHARED
    src/Core/Engine.cpp
    src/Core/Window.cpp
    src/Renderer/TextRenderer.cpp
    src/Renderer/Renderer.cpp
    # ResManager
    src/ResManager/Pak/Pak.cpp
    src/ResManager/Spr/Spr.cpp
    src/ResManager/Map/Map.cpp
    src/ResManager/Map/Region/RegionC.cpp
    src/ResManager/Map/Region/RegionS.cpp
    src/ResManager/VFS/VFS.cpp
    # Scene
    src/Scene/SceneManager.cpp
    # ECS
    src/ECS/Object/Object.cpp
    src/ECS/Components/SpriteComponent.cpp
    src/ECS/Components/ScriptComponent.cpp
    src/ECS/Scene/Scene.cpp
)
target_compile_definitions(SEngine PRIVATE
    SENGINE_EXPORTS
    # Compile cả hai nhánh client + server vào DLL
    # để DGClient và DGServer đều link được
    SENGINE_CLIENT
    SENGINE_SERVER
    # Release build: tắt SENGINE_DEBUG_DISK, chỉ load từ PAK
    $<$<CONFIG:Release>:SENGINE_RELEASE>
    $<$<CONFIG:MinSizeRel>:SENGINE_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:SENGINE_RELEASE>
)
 
target_include_directories(SEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(SEngine
    OpenGL::GL
    glfw
    freetype
    ZLIB::ZLIB
    unofficial::lua::lua
)

# output folder for engine
set_target_properties(SEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# ===== TEST EXE =====
add_executable(SEngineTest
    src/Test/main.cpp
    src/ResManager/Pak/Pak.cpp
)

target_link_libraries(SEngineTest
    SEngine
)

# output folder for exe
set_target_properties(SEngineTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/SEngineTest
)