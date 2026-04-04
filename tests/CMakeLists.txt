if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

add_executable(five_stones_tests
    stress_concurrency_test.cc
    boundary_exception_test.cc
    mysql_concurrency_stress_test.cc
)
target_compile_features(five_stones_tests PRIVATE cxx_std_17)
target_include_directories(five_stones_tests PRIVATE "${CMAKE_SOURCE_DIR}/five-stones/include")
target_compile_options(five_stones_tests PRIVATE
    "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-include${CMAKE_CURRENT_SOURCE_DIR}/wsserver_for_tests.h>"
)#给 GCC / Clang 多加一个编译选项：-include wsserver_for_tests.h

if(TARGET GTest::gtest_main)
    target_link_libraries(five_stones_tests PRIVATE GTest::gtest_main)
else()
    target_link_libraries(five_stones_tests PRIVATE GTest::GTest GTest::Main)
endif()

target_link_libraries(five_stones_tests PRIVATE
    Threads::Threads
    Boost::system
    jsoncpp
    mysqlclient
)

add_test(NAME five_stones_tests COMMAND five_stones_tests)
