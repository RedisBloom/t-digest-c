
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
    target_link_libraries(td_test tdigest m)
    enable_testing()
    add_test(td_test td_test)

    # Capacity-boundary regression: includes tdigest.c to drive the allocation-free
    # capacity_from_compression() helper at the exact accepted/rejected boundary
    # (floor((INT_MAX-10)/6)) without allocating the ~17 GB that compression would need.
    add_executable(td_capacity_test unit/td_capacity_test.c)
    target_include_directories(td_capacity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(td_capacity_test m)
    add_test(td_capacity_test td_capacity_test)
endif()



