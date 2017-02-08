cmake_minimum_required(VERSION 3.6)
project(521lab1)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        montty.c
        test1.c
        test2.c
        test3.c
        test4.c montty12.c)

add_executable(521lab1 ${SOURCE_FILES})