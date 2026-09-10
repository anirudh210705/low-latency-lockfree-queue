cmake_minimum_required(VERSION 3.20)

project(
    low_latency_lockfree_queue
    VERSION 0.1.0
    DESCRIPTION "Bounded C++20 queues and reproducible concurrency benchmarks"
    LANGUAGES CXX
)

option(LLQ_BUILD_TESTS "Build the LLQ test suite" ON)
option(LLQ_BUILD_BENCHMARKS "Build the LLQ benchmarks" OFF)
option(LLQ_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LLQ_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(LLQ_ENABLE_ASAN AND LLQ_ENABLE_TSAN)
    message(FATAL_ERROR "LLQ_ENABLE_ASAN and LLQ_ENABLE_TSAN cannot be enabled together")
endif()

add_library(llq INTERFACE)
add_library(llq::llq ALIAS llq)

target_compile_features(llq INTERFACE cxx_std_20)
target_include_directories(
    llq
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(llq INTERFACE /W4 /permissive-)
    if(LLQ_ENABLE_ASAN OR LLQ_ENABLE_TSAN)
        message(FATAL_ERROR "The configured sanitizer options require GCC or Clang on Linux")
    endif()
else()
    target_compile_options(llq INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

    if(LLQ_ENABLE_ASAN)
        target_compile_options(llq INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(llq INTERFACE -fsanitize=address,undefined)
    elseif(LLQ_ENABLE_TSAN)
        target_compile_options(llq INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(llq INTERFACE -fsanitize=thread)
    endif()
endif()

if(LLQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LLQ_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
