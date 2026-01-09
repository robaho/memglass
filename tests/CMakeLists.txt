include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Test: shared memory
add_executable(test_shm test_shm.cpp)
target_link_libraries(test_shm PRIVATE memglass GTest::gtest_main)
add_test(NAME test_shm COMMAND test_shm)

# Test: registry
add_executable(test_registry test_registry.cpp)
target_link_libraries(test_registry PRIVATE memglass GTest::gtest_main)
add_test(NAME test_registry COMMAND test_registry)

# Test: allocator
add_executable(test_allocator test_allocator.cpp)
target_link_libraries(test_allocator PRIVATE memglass GTest::gtest_main)
add_test(NAME test_allocator COMMAND test_allocator)

# Test: integration
add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE memglass GTest::gtest_main)
add_test(NAME test_integration COMMAND test_integration)

# Test: seqlock
add_executable(test_seqlock test_seqlock.cpp)
target_link_libraries(test_seqlock PRIVATE memglass GTest::gtest_main pthread)
add_test(NAME test_seqlock COMMAND test_seqlock)
