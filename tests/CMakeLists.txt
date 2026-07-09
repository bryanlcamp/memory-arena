# Add the local third-party vendor directory for GoogleTest with an explicit binary build path token
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vendor/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest-build EXCLUDE_FROM_ALL)

# Declare your dedicated standalone testing suite binary
add_executable(memory_arena_tests
  MemoryArenaTest.cpp
)

# Link your tests against the core allocator libraries and GoogleTest fabrics
target_link_libraries(memory_arena_tests PRIVATE
  memory_arena
  gtest
  gtest_main
  Threads::Threads
)

# Route the test output executable target straight to your isolated build folder
set_target_properties(memory_arena_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Apply performance compiler suites safely to match your core production targets
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  target_compile_options(memory_arena_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O3
    -fno-exceptions
    -fno-rtti
  )
endif()