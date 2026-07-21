cmake_minimum_required(VERSION 3.22)

project(SoundVSTProject VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SOUND_VST_BUILD_TESTS "Build repository tests" ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

if(SOUND_VST_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

add_subdirectory(plugins/loop-surgeon)

