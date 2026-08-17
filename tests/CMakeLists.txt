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

add_executable(test_invariants test_invariants.cpp)
target_link_libraries(test_invariants PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_invariants)

add_executable(test_dbn_loader dbn/test_loader.cpp)
target_link_libraries(test_dbn_loader PRIVATE ob GTest::gtest_main)
set_target_properties(test_dbn_loader PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
gtest_discover_tests(test_dbn_loader WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_book_diff dbn/test_book_diff.cpp)
target_link_libraries(test_book_diff PRIVATE ob GTest::gtest_main)
set_target_properties(test_book_diff PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
gtest_discover_tests(test_book_diff WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_itch_messages itch/test_messages.cpp)
target_link_libraries(test_itch_messages PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_itch_messages)

add_executable(test_itch_parser itch/test_parser.cpp)
target_link_libraries(test_itch_parser PRIVATE ob GTest::gtest_main)
gtest_discover_tests(test_itch_parser)
