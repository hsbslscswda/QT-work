cmake_minimum_required(VERSION 3.21)

project(Xinqing VERSION 5.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

add_library(xinqing_ui STATIC
    mainwindow.h
    mainwindow.cpp
    mainwindow.ui
    resultloader.h
    resultloader.cpp
    apiendpoint.cpp apiendpoint.h
)

target_link_libraries(xinqing_ui PUBLIC Qt6::Widgets Qt6::Network)
target_include_directories(xinqing_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(Xinqing main.cpp resources.qrc)
target_link_libraries(Xinqing PRIVATE xinqing_ui)

if(WIN32)
    set_target_properties(Xinqing PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_compile_options(Xinqing PRIVATE /utf-8)
        target_compile_options(xinqing_ui PRIVATE /utf-8)
    endif()
endif()

install(TARGETS Xinqing RUNTIME DESTINATION bin)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_xinqing ../tests/test_xinqing.cpp resources.qrc)
    target_link_libraries(test_xinqing PRIVATE xinqing_ui Qt6::Test)
    if(MSVC)
        target_compile_options(test_xinqing PRIVATE /utf-8)
    endif()
    add_test(NAME xinqing_ui COMMAND test_xinqing)
    set_tests_properties(xinqing_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
