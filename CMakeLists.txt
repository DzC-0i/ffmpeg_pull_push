cmake_minimum_required(VERSION 3.10)
project(VideoStreamer)

set(CMAKE_CXX_STANDARD 14)

# 查找 OpenCV 库
find_package(OpenCV REQUIRED)
# 查找 FFmpeg 库
find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
    # libavdevice
)

# 添加头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${LIBAV_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    )
# 添加源文件
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/ffmpeg_capture.cpp
    ${CMAKE_SOURCE_DIR}/src/ffmpeg_metwork_init.cpp
    ${CMAKE_SOURCE_DIR}/src/ffmpeg_pusher.cpp
    ${CMAKE_SOURCE_DIR}/src/main.cpp
)
# 添加可执行文件
add_executable(video_streamer ${SOURCES})

# 链接库
target_link_libraries(video_streamer
    ${LIBAV_LIBRARIES}
    ${OpenCV_LIBS}
    pthread
)

# # 添加调试信息
# add_definitions(-g -O0 -ggdb -gdwarf -funwind-tables -rdynamic)

# # 设置安装路径
# install(TARGETS video_streamer DESTINATION bin)
