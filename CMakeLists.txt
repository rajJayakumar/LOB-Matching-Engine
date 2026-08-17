cmake_minimum_required(VERSION 3.20)
project(order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# databento-cpp — DBN decoder only (no live/historical HTTP client)
FetchContent_Declare(
  databento_cpp
  GIT_REPOSITORY https://github.com/databento/databento-cpp.git
  GIT_TAG        v0.22.0
)
FetchContent_MakeAvailable(databento_cpp)

add_library(ob
    src/order_book.cpp
    src/dbn/loader.cpp
)
target_include_directories(ob PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ob PUBLIC databento::databento)

# Apps
add_executable(dbn_diff apps/dbn_diff_main.cpp)
target_link_libraries(dbn_diff PRIVATE ob)

# Testing
enable_testing()
add_subdirectory(tests)
