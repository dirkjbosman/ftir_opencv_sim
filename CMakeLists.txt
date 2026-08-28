cmake_minimum_required(VERSION 3.10)
project(FTIR-PLUME-SIMULATOR)

set(CMAKE_CXX_STANDARD 17)

# Find OpenCV package
find_package(OpenCV REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(frame_generator frame_generator.cpp)
target_link_libraries(frame_generator ${OpenCV_LIBS})

add_executable(tracker_engine tracker_engine.cpp)
target_link_libraries(tracker_engine ${OpenCV_LIBS})