cmake_minimum_required(VERSION 3.20)
project(order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only for now; library target added when source files arrive.
add_library(ob INTERFACE)
target_include_directories(ob INTERFACE ${CMAKE_SOURCE_DIR}/include)

# Testing
enable_testing()
add_subdirectory(tests)
