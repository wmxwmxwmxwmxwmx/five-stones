if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

if(NOT DEFINED FIVE_STONES_WITH_REDIS)
    option(FIVE_STONES_WITH_REDIS "Enable redis-plus-plus backend if available" ON)
endif()
# 用于清除 CMake 缓存中的变量：为了强制重新查找依赖库，避免使用过时或错误的缓存值
unset(REDISPP_INCLUDE_DIR CACHE)
unset(REDISPP_LIBRARY CACHE)
unset(HIREDIS_LIBRARY CACHE)

if(FIVE_STONES_WITH_REDIS)
    find_path(REDISPP_INCLUDE_DIR sw/redis++/redis++.h)
    find_library(REDISPP_LIBRARY NAMES redis++)
    find_library(HIREDIS_LIBRARY NAMES hiredis)
    if(NOT REDISPP_INCLUDE_DIR OR NOT REDISPP_LIBRARY OR NOT HIREDIS_LIBRARY)
        message(WARNING "redis-plus-plus/hiredis not found, disabling Redis cache backend for this build")
        set(FIVE_STONES_WITH_REDIS OFF)
    endif()
endif()

add_executable(gobang_server src/main.cc)

# 添加包含目录
target_include_directories(gobang_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(FIVE_STONES_WITH_REDIS)
    target_include_directories(gobang_server PRIVATE ${REDISPP_INCLUDE_DIR})
    target_compile_definitions(gobang_server PRIVATE FIVE_STONES_WITH_REDIS=1)
endif()

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
if(FIVE_STONES_WITH_REDIS)
    target_link_libraries(gobang_server PRIVATE
        ${REDISPP_LIBRARY}
        ${HIREDIS_LIBRARY}
    )
endif()

# 设置输出目录
set_target_properties(gobang_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
