cmake_minimum_required(VERSION 3.16)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

set(CMAKE_AUTOMOC ON)

add_executable(test-power-idlewatcher test_idlewatcher.cpp)
target_include_directories(test-power-idlewatcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin-qt/power/session
    ${CMAKE_SOURCE_DIR}/src/plugin-qt/power/session/idle
)
target_link_libraries(test-power-idlewatcher PRIVATE Qt${QT_VERSION_MAJOR}::Test)

add_executable(test-power-screencontroller test_screencontroller.cpp)
target_include_directories(test-power-screencontroller PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin-qt/power/session
    ${CMAKE_SOURCE_DIR}/src/plugin-qt/power/session/screen
)
target_link_libraries(test-power-screencontroller PRIVATE Qt${QT_VERSION_MAJOR}::Test)
