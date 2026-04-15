cmake_minimum_required(VERSION 3.20)

project(handDemo LANGUAGES CXX)

# 只支持 Windows，因为当前串口实现直接依赖 Win32 API。
if(NOT WIN32)
    message(FATAL_ERROR "当前项目仅支持 Windows 平台构建。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# opencvRootPath: 当前仓库内的 OpenCV 目录。
set(opencvRootPath "${CMAKE_CURRENT_SOURCE_DIR}/opencv")
# opencvIncludePath/opencvLibPath: 直接使用仓库内 include/lib，避免依赖系统安装路径。
set(opencvIncludePath "${opencvRootPath}/include")
set(opencvLibPath "${opencvRootPath}/lib")

if(NOT EXISTS "${opencvIncludePath}")
    message(FATAL_ERROR "未找到 OpenCV 头文件目录: ${opencvIncludePath}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(opencvWorldLibName "opencv_world480d.lib")
    set(opencvWorldDllName "opencv_world480d.dll")
else()
    set(opencvWorldLibName "opencv_world480.lib")
    set(opencvWorldDllName "opencv_world480.dll")
endif()

set(opencvWorldLibPath "${opencvLibPath}/${opencvWorldLibName}")
if(NOT EXISTS "${opencvWorldLibPath}")
    message(FATAL_ERROR "未找到 OpenCV 库文件: ${opencvWorldLibPath}")
endif()

add_executable(handDemo
    hand_algorithm.cpp
    hand_skeleton_viewer.cpp
    main.cpp
    serial_port_io.cpp
)

target_include_directories(handDemo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${opencvIncludePath}"
)

target_compile_options(handDemo PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(handDemo PRIVATE
    "${opencvWorldLibPath}"
    Setupapi
    Advapi32
)

# opencvDllSearchPathList: 运行时 DLL 的候选目录。
# 优先仓库内常见位置，其次兼容旧工程里已经存在的 OpenCV 安装路径。
set(opencvDllSearchPathList
    "${opencvRootPath}/bin/${opencvWorldDllName}"
    "${opencvRootPath}/x64/vc16/bin/${opencvWorldDllName}"
    "${opencvRootPath}/build/x64/vc16/bin/${opencvWorldDllName}"
    "D:/yhc_code/opencv/build/x64/vc16/bin/${opencvWorldDllName}"
)

set(foundOpenCvDllPath "")
foreach(candidateDllPath IN LISTS opencvDllSearchPathList)
    if(EXISTS "${candidateDllPath}")
        set(foundOpenCvDllPath "${candidateDllPath}")
        break()
    endif()
endforeach()

# 如果能找到 DLL，就在构建后自动复制到输出目录，保证直接运行。
if(foundOpenCvDllPath)
    add_custom_command(TARGET handDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${foundOpenCvDllPath}"
            "$<TARGET_FILE_DIR:handDemo>/${opencvWorldDllName}"
        COMMENT "复制 OpenCV DLL 到输出目录"
    )
else()
    message(WARNING "未在仓库内找到 ${opencvWorldDllName}，编译可继续，但运行前需要把 DLL 放到可执行文件目录或 PATH。")
endif()
