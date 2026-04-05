#include "server.hpp"
#include "app_config.hpp"
#include "logger.hpp"

#include <cstdint>
#include <exception>
#include <string>

int main(int argc, char *argv[])
{
    ServerCfg cfg;
    std::string err;
    if (!load_server_cfg(argc, argv, cfg, std::string(WWWROOT), &err))
    {
        ERR_LOG("configuration error: %s", err.c_str());
        if (argc > 2)
            ERR_LOG("Usage: %s [server_port]", argv[0]);
        return 1;
    }

    try
    {
        gobang_server server(cfg.db_host, cfg.db_user, cfg.db_pass, cfg.db_name, cfg.db_port, cfg.wwwroot);
        INF_LOG("gobang server starting on port: %d", cfg.listen_port);
        server.start(cfg.listen_port);
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
