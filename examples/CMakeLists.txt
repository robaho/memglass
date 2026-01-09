# Generate trading_types_generated.hpp from trading_types.hpp into build directory
set(TRADING_TYPES_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/trading_types_generated.hpp)

add_custom_command(
    OUTPUT ${TRADING_TYPES_GENERATED}
    COMMAND memglass-gen
        ${CMAKE_CURRENT_SOURCE_DIR}/trading_types.hpp
        -I ${CMAKE_SOURCE_DIR}/include
        -o ${TRADING_TYPES_GENERATED}
    DEPENDS
        memglass-gen
        ${CMAKE_CURRENT_SOURCE_DIR}/trading_types.hpp
    COMMENT "Generating trading_types_generated.hpp"
)

add_custom_target(trading_types_gen
    DEPENDS ${TRADING_TYPES_GENERATED}
)

add_executable(trading_producer trading_producer.cpp)
target_link_libraries(trading_producer PRIVATE memglass)
target_include_directories(trading_producer PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(trading_producer trading_types_gen)

add_executable(trading_observer trading_observer.cpp)
target_link_libraries(trading_observer PRIVATE memglass)

