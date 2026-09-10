find_package(Threads REQUIRED)

add_executable(llq_tests mutex_queue_test.cpp)
target_link_libraries(llq_tests PRIVATE llq::llq Threads::Threads)

add_test(NAME llq_tests COMMAND llq_tests)
