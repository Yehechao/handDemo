cmake_minimum_required(VERSION 3.20)

project(handDemo LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "当前项目仅支持 Windows 平台构建。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(handDemo
    hand_algorithm.cpp
    main.cpp
    serial_port_io.cpp
)

target_include_directories(handDemo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_options(handDemo PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(handDemo PRIVATE
    Setupapi
    Advapi32
)
