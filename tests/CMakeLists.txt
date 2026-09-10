find_package(Threads REQUIRED)

foreach(test_name IN ITEMS mutex_queue spsc_queue mpmc_queue)
    add_executable(${test_name}_test ${test_name}_test.cpp)
    target_link_libraries(${test_name}_test PRIVATE llq::llq Threads::Threads)
    add_test(NAME ${test_name}_test COMMAND ${test_name}_test)
endforeach()
