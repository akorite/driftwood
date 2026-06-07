add_executable(perft_test perft_test.cpp)
target_link_libraries(perft_test PRIVATE driftwood gtest_main)
add_test(NAME perft_test COMMAND perft_test)
set_tests_properties(perft_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE driftwood gtest_main)
add_test(NAME engine_test COMMAND engine_test)
set_tests_properties(engine_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
