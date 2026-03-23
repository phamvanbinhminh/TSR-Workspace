project(DGServer)

find_package(unofficial-lua REQUIRED)

add_executable(DGServer
    src/Core/DGServer.cpp
    src/Auth/UserManager.cpp
    src/Auth/CharacterManager.cpp
    src/Network/LoginServer.cpp
    src/Network/GameServer.cpp
)

target_include_directories(DGServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    # SEngine headers (Map, RegionS, ...)
    ${CMAKE_SOURCE_DIR}/SEngine/src
)

target_compile_definitions(DGServer PRIVATE
    SENGINE_SERVER
)

target_link_libraries(DGServer
    SEngine
    ws2_32
    unofficial::lua::lua
)

set_target_properties(DGServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/DGServer
)
