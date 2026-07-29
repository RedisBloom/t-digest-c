
if (BUILD_BENCHMARK)
    if (UNIX)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -g -ggdb -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2  -g -ggdb -fno-omit-frame-pointer")
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing benchmark's tests" FORCE)
        add_subdirectory(vendor/google/benchmark)
        include_directories(vendor/google/benchmark/include)
        add_executable(histogram_benchmark benchmark/histogram_benchmark.cpp)
        target_link_libraries(histogram_benchmark tdigest benchmark::benchmark)
    else()
        message(WARNING
              "google.benchmark - microbenchmarks disabled on WIN32 platforms")
    endif()
endif()
if (BUILD_TESTS)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -std=c99")
    add_executable(td_test unit/td_test.c unit/minunit.h)
    # minunit.h uses clock_gettime(), which the top-level "-std=c99" hides unless
    # _POSIX_C_SOURCE is defined on the command line (before any header is included).
    # Without this the td_test build fails with "unknown type name 'clockid_t'".
    target_compile_definitions(td_test PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(td_test tdigest m)
    enable_testing()
    add_test(td_test td_test)

    # Capacity-boundary regression: includes tdigest.c to drive the allocation-free
    # capacity_from_compression() helper at the exact accepted/rejected boundary without
    # allocating the ~34 GB (~32 GiB) that the largest accepted compression would need.
    # Use CMAKE_CURRENT_LIST_DIR so the src path stays correct when t-digest-c is consumed
    # via add_subdirectory() (CMAKE_SOURCE_DIR would point at the outermost project).
    add_executable(td_capacity_test unit/td_capacity_test.c)
    target_include_directories(td_capacity_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src)
    target_link_libraries(td_capacity_test m)
    add_test(td_capacity_test td_capacity_test)
endif()



