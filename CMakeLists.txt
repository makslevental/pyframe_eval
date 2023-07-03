cmake_minimum_required(VERSION 3.25)
project(pyframe_eval LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(pyframe_eval main.cpp eval_frame.cpp cpython_defs.cpp)
#add_executable(pyframe_eval main.cpp eval_frame.cpp cpython_defs.cpp)
#target_include_directories(pyframe_eval PRIVATE ${Python3_INCLUDE_DIRS})
#target_link_libraries(pyframe_eval ${Python3_LIBRARIES})
