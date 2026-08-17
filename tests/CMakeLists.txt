include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(test_sanity test_sanity.cpp)
target_link_libraries(test_sanity PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_sanity)
