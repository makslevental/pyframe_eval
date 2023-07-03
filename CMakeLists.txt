cmake_minimum_required(VERSION 3.25)
project(pyframe_eval LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(pyframe_eval eval_frame.cpp cpython_defs.cpp)

install(TARGETS pyframe_eval
        LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}")