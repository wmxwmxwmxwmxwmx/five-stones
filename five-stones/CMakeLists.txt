find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

add_executable(gobang_server
    src/main.cc
)
# 添加包含目录
target_include_directories(gobang_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 静态资源根目录：源码树内 resources/wwwroot，避免依赖进程工作目录
target_compile_definitions(gobang_server PRIVATE
    "WWWROOT=\"${CMAKE_CURRENT_SOURCE_DIR}/resources/wwwroot/\""
)

# 添加编译选项
# -Wall打开一大类常用告警，-Wextra打开一些不常用的告警
target_compile_options(gobang_server PRIVATE
    -Wall -Wextra
)
# 添加链接库,jsoncpp和mysqlclient是第三方库，需要手动添加
target_link_libraries(gobang_server PRIVATE
    Threads::Threads
    Boost::system
    jsoncpp
    mysqlclient
)

# 设置输出目录
set_target_properties(gobang_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
