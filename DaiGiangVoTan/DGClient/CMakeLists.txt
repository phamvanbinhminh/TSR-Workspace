project(DGClient)

# Đảm bảo MSVC đọc source file là UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

#có thể dùng jit
find_package(unofficial-lua REQUIRED)

add_executable(DGClient
    src/Core/DGClient.cpp
    src/Scene/LoginScene.cpp
    src/Scene/GameScene.cpp
    src/Network/LoginClient.cpp
    src/Network/GameClient.cpp
)

target_include_directories(DGClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    # SEngine headers (VFS, IScene, SceneManager, ...)
    ${CMAKE_SOURCE_DIR}/SEngine/src
)

target_compile_definitions(DGClient PRIVATE
    SENGINE_CLIENT
    # Release build: tắt SENGINE_DEBUG_DISK, chỉ load từ PAK
    $<$<CONFIG:Release>:SENGINE_RELEASE>
    $<$<CONFIG:MinSizeRel>:SENGINE_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:SENGINE_RELEASE>
)

target_link_libraries(DGClient
    SEngine
    ws2_32
    unofficial::lua::lua
)

set_target_properties(DGClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/DGClient
)

# ── Copy Lua scripts vào thư mục output sau khi build ─────────────────────────
# dev fallback: scripts/player.lua → bin/DGClient/res/paks/scripts/entities/player/player.lua
add_custom_command(TARGET DGClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DGClient>/res/paks/scripts/entities/player
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ECS/Scripts/entities/player/player.lua
        $<TARGET_FILE_DIR:DGClient>/res/paks/scripts/entities/player/player.lua
    COMMENT "Copying Lua scripts to output directory"
)
