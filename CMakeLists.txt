cmake_minimum_required(VERSION 3.10)
project(MeMo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(memo
    main.cpp
)

# include directories
target_include_directories(memo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# pthread for threading and atomics on some platforms
find_package(Threads REQUIRED)
target_link_libraries(memo PRIVATE Threads::Threads)
