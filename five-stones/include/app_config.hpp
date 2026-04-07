// 服务端运行时配置：与压测共用 apply_env_override；load_server_cfg 供 main 使用。
// MySQL 环境变量与 tests/mysql_concurrency_stress_test.cc 中 load_cfg 一致。

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

inline void apply_env_override(std::string &dst, const char *env_name)
{
    const char *v = std::getenv(env_name);
    if (v != nullptr && v[0] != '\0')
        dst = v;
}

struct ServerCfg
{
    std::string db_host{"127.0.0.1"};
    std::string db_user{"wmx"};
    std::string db_pass{"123456"};
    std::string db_name{"online_gobang"};
    uint16_t db_port{3306};
    std::string redis_host{"127.0.0.1"};
    uint16_t redis_port{6379};
    std::string redis_password{};
    int redis_db{0};
    int redis_timeout_ms{200};
    int listen_port{8080};
    std::string wwwroot;
};

// wwwroot_compile_default：与编译期 WWWROOT 宏一致（main 在包含 server.hpp 后传入 std::string(WWWROOT)）。
// 失败时返回 false，err_out 为英文短语便于日志；成功时 cfg 填好。
inline bool load_server_cfg(int argc, char *argv[], ServerCfg &cfg, const std::string &wwwroot_compile_default,
                            std::string *err_out = nullptr)
{
    auto fail = [err_out](const char *msg) -> bool {
        if (err_out != nullptr)
            *err_out = msg;
        return false;
    };

    //MySQL默认配置
    cfg.db_host = "127.0.0.1";
    cfg.db_user = "wmx";
    cfg.db_pass = "123456";
    cfg.db_name = "online_gobang";
    cfg.db_port = 3306;
    //Redis默认配置
    cfg.redis_host = "127.0.0.1";
    cfg.redis_port = 6379;
    cfg.redis_password.clear();
    cfg.redis_db = 0;
    cfg.redis_timeout_ms = 200;
    cfg.listen_port = 8080;
    //静态资源默认配置
    cfg.wwwroot = wwwroot_compile_default;

    //MySQL配置
    apply_env_override(cfg.db_host, "MYSQL_HOST");
    apply_env_override(cfg.db_user, "MYSQL_USER");
    apply_env_override(cfg.db_pass, "MYSQL_PASSWORD");
    apply_env_override(cfg.db_name, "MYSQL_DATABASE");

    if (const char *p = std::getenv("MYSQL_PORT"))
    {
        if (p[0] == '\0')
            return fail("MYSQL_PORT is empty");
        try
        {
            const int port = std::stoi(p);
            if (port <= 0 || port > 65535)
                return fail("MYSQL_PORT out of range");
            cfg.db_port = static_cast<uint16_t>(port);
        }
        catch (...)
        {
            return fail("MYSQL_PORT invalid");
        }
    }

    //Redis配置
    apply_env_override(cfg.redis_host, "REDIS_HOST");
    apply_env_override(cfg.redis_password, "REDIS_PASSWORD");
    if (const char *p = std::getenv("REDIS_PORT"))
    {
        if (p[0] == '\0')
            return fail("REDIS_PORT is empty");
        try
        {
            const int port = std::stoi(p);
            if (port <= 0 || port > 65535)
                return fail("REDIS_PORT out of range");
            cfg.redis_port = static_cast<uint16_t>(port);
        }
        catch (...)
        {
            return fail("REDIS_PORT invalid");
        }
    }
    if (const char *p = std::getenv("REDIS_DB"))
    {
        if (p[0] == '\0')
            return fail("REDIS_DB is empty");
        try
        {
            const int db = std::stoi(p);
            if (db < 0)
                return fail("REDIS_DB out of range");
            cfg.redis_db = db;
        }
        catch (...)
        {
            return fail("REDIS_DB invalid");
        }
    }
    if (const char *p = std::getenv("REDIS_TIMEOUT_MS"))
    {
        if (p[0] == '\0')
            return fail("REDIS_TIMEOUT_MS is empty");
        try
        {
            const int t = std::stoi(p);
            if (t <= 0)
                return fail("REDIS_TIMEOUT_MS out of range");
            cfg.redis_timeout_ms = t;
        }
        catch (...)
        {
            return fail("REDIS_TIMEOUT_MS invalid");
        }
    }

    //静态资源配置
    apply_env_override(cfg.wwwroot, "WWWROOT");

    //命令行参数配置
    if (argc > 2)
        return fail("too many arguments");

    if (argc == 2)
    {
        try
        {
            const int p = std::stoi(argv[1]);
            if (p < 1 || p > 65535)
                return fail("server port out of range");
            cfg.listen_port = p;
        }
        catch (...)
        {
            return fail("invalid server port argument");
        }
    }
    else
    {
        const char *sp = std::getenv("SERVER_PORT");
        if (sp != nullptr && sp[0] != '\0')
        {
            try
            {
                const int p = std::stoi(sp);
                if (p < 1 || p > 65535)
                    return fail("SERVER_PORT out of range");
                cfg.listen_port = p;
            }
            catch (...)
            {
                return fail("SERVER_PORT invalid");
            }
        }
    }

    return true;
}
