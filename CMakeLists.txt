cmake_minimum_required(VERSION 3.24)
project(pyframe_eval LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_pyframe_eval cpp_ext/evalFrame.cpp cpp_ext/cpythonDefs.cpp)

install(TARGETS _pyframe_eval
        LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}")