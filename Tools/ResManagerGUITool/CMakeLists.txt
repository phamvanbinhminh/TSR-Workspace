project(SResManager) 
find_package(imgui CONFIG REQUIRED) 
add_executable(SResManager
 src/Core/main.cpp src/MapEdit/MapEditor.cpp 
 src/PakEdit/PakEditor.cpp 
 src/SprEdit/SprEditor.cpp
 ${CMAKE_SOURCE_DIR}/Tools/external/imgui/backends/imgui_impl_glfw.cpp
 ${CMAKE_SOURCE_DIR}/Tools/external/imgui/backends/imgui_impl_opengl3.cpp
 ) 

target_include_directories(SResManager PRIVATE
    ${CMAKE_SOURCE_DIR}/SEngine/src
    ${CMAKE_SOURCE_DIR}/Tools/ResManagerGUITool/src

    ${CMAKE_SOURCE_DIR}/Tools/external/imgui
    ${CMAKE_SOURCE_DIR}/Tools/external/imgui/backends
)
target_link_libraries(SResManager SEngine imgui::imgui ) 

set_target_properties(SResManager PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/SResManager )
target_compile_definitions(SResManager PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
)