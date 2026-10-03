cmake_minimum_required(VERSION 3.16)
project(winject-radio-realtek CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WINJECT_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src/radio)

if(WINJECT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/test)
endif()
