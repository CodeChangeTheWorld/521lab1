cmake_minimum_required(VERSION 3.6)
project(521lab1)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        montty1.c
        test1.c
        test2.c
        test3.c
        test4.c montty.c)

add_executable(521lab1 ${SOURCE_FILES})