#include "server.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
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
        std::cerr << "Usage: " << argv[0] << " [server_port]" << std::endl;
        return 1;
    }

    try
    {
        gobang_server server(db_host, db_user, db_pass, db_name, db_port);
        std::cout << "gobang server starting on port: " << server_port << std::endl;
        server.start(server_port);
    }
    catch (const std::exception &e)
    {
        std::cerr << "server fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "server fatal error: unknown exception" << std::endl;
        return 1;
    }

    return 0;
}
