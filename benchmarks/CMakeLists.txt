find_package(Threads REQUIRED)

add_executable(queue_benchmark queue_benchmark.cpp)
target_link_libraries(queue_benchmark PRIVATE llq::llq Threads::Threads)
