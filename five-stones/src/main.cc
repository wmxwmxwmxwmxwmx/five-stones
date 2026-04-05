#include "server.hpp"
#include "logger.hpp"

#include <cstdint>
#include <exception>
#include <string>

int main(int argc, char *argv[])
{
    // 默认配置（可按需改成你自己的数据库环境）
    std::string db_host = "127.0.0.1";
    std::string db_user = "wmx";
    std::string db_pass = "123456";
    std::string db_name = "online_gobang";
    uint16_t db_port = 3306;
    int server_port = 8080;

    // 支持命令行覆盖：
    // ./server <server_port>
    if (argc == 2)
        server_port = std::stoi(argv[1]);
    else if (argc > 2)
    {
        ERR_LOG("Usage: %s [server_port]", argv[0]);
        return 1;
    }

    try
    {
        gobang_server server(db_host, db_user, db_pass, db_name, db_port);
        INF_LOG("gobang server starting on port: %d", server_port);
        server.start(server_port);
    }
    catch (const std::exception &e)
    {
        ERR_LOG("server fatal error: %s", e.what());
        return 1;
    }
    catch (...)
    {
        ERR_LOG("server fatal error: unknown exception");
        return 1;
    }

    return 0;
}
