cmake_minimum_required(VERSION 3.16)

# Find libclang
find_library(LIBCLANG_LIBRARY
    NAMES clang libclang
    PATHS
        /usr/lib/llvm-14/lib
        /usr/lib/llvm-15/lib
        /usr/lib/llvm-16/lib
        /usr/lib/llvm-17/lib
        /usr/lib/llvm-18/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib
        /usr/local/lib
)

find_path(LIBCLANG_INCLUDE_DIR
    NAMES clang-c/Index.h
    PATHS
        /usr/lib/llvm-14/include
        /usr/lib/llvm-15/include
        /usr/lib/llvm-16/include
        /usr/lib/llvm-17/include
        /usr/lib/llvm-18/include
        /usr/include
        /usr/local/include
)

if(NOT LIBCLANG_LIBRARY)
    message(FATAL_ERROR "libclang library not found. Install libclang-dev.")
endif()
if(NOT LIBCLANG_INCLUDE_DIR)
    message(FATAL_ERROR "libclang headers not found. Install libclang-dev.")
endif()

message(STATUS "Found libclang: ${LIBCLANG_LIBRARY}")
message(STATUS "Found libclang headers: ${LIBCLANG_INCLUDE_DIR}")

add_executable(memglass-gen
    main.cpp
    generator.cpp
)

target_include_directories(memglass-gen PRIVATE
    ${LIBCLANG_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(memglass-gen PRIVATE
    ${LIBCLANG_LIBRARY}
    fmt::fmt
)

install(TARGETS memglass-gen RUNTIME DESTINATION bin)
