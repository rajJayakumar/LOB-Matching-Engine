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

add_executable(test_matching test_matching.cpp)
target_link_libraries(test_matching PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_matching)

add_executable(test_order_types test_order_types.cpp)
target_link_libraries(test_order_types PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_order_types)

add_executable(test_cancel test_cancel.cpp)
target_link_libraries(test_cancel PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_cancel)

add_executable(test_edge_cases test_edge_cases.cpp)
target_link_libraries(test_edge_cases PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_edge_cases)
