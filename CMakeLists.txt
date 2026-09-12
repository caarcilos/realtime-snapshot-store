cmake_minimum_required(VERSION 3.20)

project(lock_free_store_sample LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(ENABLE_TSAN "Build the tests with ThreadSanitizer" OFF)
option(ENABLE_ASAN_UBSAN "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(ENABLE_TSAN AND ENABLE_ASAN_UBSAN)
    message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be enabled together")
endif()

add_library(lock_free_store INTERFACE)
add_library(realtime::lock_free_store ALIAS lock_free_store)

target_sources(lock_free_store INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/realtime/lock_free_store.hpp>"
)

target_include_directories(lock_free_store INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_compile_features(lock_free_store INTERFACE cxx_std_20)

if(BUILD_TESTING)
    add_executable(lock_free_store_tests
        tests/lock_free_store_test.cpp
    )

    find_package(Threads REQUIRED)
    target_link_libraries(lock_free_store_tests PRIVATE
        realtime::lock_free_store
        Threads::Threads
    )

    if(MSVC)
        target_compile_options(lock_free_store_tests PRIVATE
            /W4
            /permissive-
        )

        if(WARNINGS_AS_ERRORS)
            target_compile_options(lock_free_store_tests PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lock_free_store_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

        if(WARNINGS_AS_ERRORS)
            target_compile_options(lock_free_store_tests PRIVATE -Werror)
        endif()
    endif()

    if(ENABLE_TSAN OR ENABLE_ASAN_UBSAN)
        if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "The configured sanitizer requires GCC or Clang")
        endif()

        set(SANITIZER_FLAGS -fno-omit-frame-pointer)

        if(ENABLE_TSAN)
            list(APPEND SANITIZER_FLAGS -fsanitize=thread)
        else()
            list(APPEND SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all)
        endif()

        target_compile_options(lock_free_store_tests PRIVATE ${SANITIZER_FLAGS})
        target_link_options(lock_free_store_tests PRIVATE ${SANITIZER_FLAGS})
    endif()

    add_test(NAME lock_free_store_tests COMMAND lock_free_store_tests)
endif()
