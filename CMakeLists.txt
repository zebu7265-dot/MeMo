cmake_minimum_required(VERSION 3.10)
project(MeMo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(memo
    main.cpp
    core/cold_storage.cpp
    core/hot_cache.cpp
    core/memory_object.cpp
)

# include directories
target_include_directories(memo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(memo PRIVATE Threads::Threads)

enable_testing()

add_executable(memo_storage_test
    tests/test_storage.cpp
    core/memory_object.cpp
    core/cold_storage.cpp
    core/hot_cache.cpp
)
target_include_directories(memo_storage_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(memo_storage_test PRIVATE Threads::Threads)
add_test(NAME memo_storage_test COMMAND memo_storage_test)
