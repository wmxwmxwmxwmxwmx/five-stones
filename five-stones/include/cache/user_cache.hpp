#pragma once

#include <cstdint>
#include <string>

#include <jsoncpp/json/json.h>
// 定义了用户缓存操作
class UserCache
{
public:
    virtual ~UserCache() = default;
    virtual bool getUserById(uint64_t uid, Json::Value &user) = 0;
    virtual bool getUserByName(const std::string &name, Json::Value &user) = 0;
    virtual bool setUser(uint64_t uid, const std::string &name, const Json::Value &user, int ttlSec) = 0;
    virtual bool invalidateById(uint64_t uid) = 0;
    virtual bool invalidateByName(const std::string &name) = 0;
};
// 提供 NoopUserCache（全返回 false）
// 用途：Redis 不可用时的降级实现，避免程序崩溃。
class NoopUserCache : public UserCache
{
public:
    bool getUserById(uint64_t, Json::Value &) override { return false; }
    bool getUserByName(const std::string &, Json::Value &) override { return false; }
    bool setUser(uint64_t, const std::string &, const Json::Value &, int) override { return false; }
    bool invalidateById(uint64_t) override { return false; }
    bool invalidateByName(const std::string &) override { return false; }
};
