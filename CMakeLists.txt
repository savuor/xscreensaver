cmake_minimum_required(VERSION 3.5)

project(analogtv-xscr C CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wno-overlength-strings -std=c++17")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS} -O2")

find_package(OpenCV)
find_package(wxWidgets REQUIRED COMPONENTS net core base CONFIG)

include_directories(
    "./src"
    ${OpenCV_INCLUDE_DIRS}
    ${wxWidgets_USE_FILE}
)

add_executable(analogtv-cli
    "./src/analogtv-cli.cpp"
    "./src/source.cpp"
    "./src/output.cpp"
    "./src/analogtv.cpp"
    "./src/utils.cpp"
)

target_link_libraries(analogtv-cli ${OpenCV_LIBS} ${wxWidgets_LIBRARIES})
