cmake_minimum_required(VERSION 3.10)

project(TSR)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(SEngine)
add_subdirectory(DaiGiangVoTan/DGClient)
add_subdirectory(DaiGiangVoTan/DGServer)
add_subdirectory(Tools/ResManagerGUITool)