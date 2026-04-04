cmake_minimum_required(VERSION 3.16)

project(five_stones LANGUAGES CXX)

#必须用 C++17，且尽量别用编译器私货扩展
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(five-stones)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        set(FIVE_STONES_GOOGLETEST_REPOSITORY
            "https://github.com/google/googletest.git"
            CACHE STRING
            "googletest Git URL (若无法访问 GitHub 可改为 https://gitee.com/mirrors/googletest.git)")
        include(FetchContent)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY "${FIVE_STONES_GOOGLETEST_REPOSITORY}"
            GIT_TAG v1.14.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    add_subdirectory(tests)
endif()
