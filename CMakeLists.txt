cmake_minimum_required(VERSION 3.5)

project(analogtv-xscr C CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wno-overlength-strings -pthread")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -pedantic -Wall -Wno-overlength-strings -pthread -O2")

find_package(OpenCV)

include_directories(
    "./src"
    ${OpenCV_INCLUDE_DIRS}
)

link_libraries(
    "pthread"
    "m"
)

add_executable(analogtv-cli
    "./src/analogtv-cli.cpp"
    "./src/analogtv.cpp"
    "./src/yarandom.cpp"
    "./src/aligned_malloc.cpp"
    "./src/thread_util.cpp"
)

target_link_libraries(analogtv-cli ${OpenCV_LIBS})
