#pragma once

#include "cache/cache_metrics.hpp"
#include "cache/user_cache.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <memory>
#include <string>

#include <sw/redis++/redis++.h>

class RedisUserCache : public UserCache
{
public:
    explicit RedisUserCache(const std::shared_ptr<sw::redis::Redis> &redis) : _redis(redis) {}

    // 获取用户信息
    bool getUserById(uint64_t uid, Json::Value &user) override
    {
        return getByKey(keyById(uid), user);
    }

    // 获取用户信息
    bool getUserByName(const std::string &name, Json::Value &user) override
    {
        return getByKey(keyByName(name), user);
    }

    // 设置用户信息
    bool setUser(uint64_t uid, const std::string &name, const Json::Value &user, int ttlSec) override
    {
        std::string body;
        if (!json_util::serialize(user, body)) // 序列化用户信息
            return false;
        try
        {
            _redis->set(keyById(uid), body);    // 设置id-key
            _redis->set(keyByName(name), body); // 设置name-key
            if (ttlSec > 0)
            {
                _redis->expire(keyById(uid), ttlSec);    // 设置id-key超时时间
                _redis->expire(keyByName(name), ttlSec); // 设置name-key超时时间
            }
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed);
            ERR_LOG("redis setUser failed: %s", e.what());
            return false;
        }
    }

    // 删除用户信息
    // invalidateById 的做法是：
    // 1.先读 id-key
    // 2.从 value 里拿 username
    // 3.删 name-key
    // 4.再删 id-key
    bool invalidateById(uint64_t uid) override
    {
        try
        {
            const std::string k = keyById(uid); // 生成用户ID键
            auto v = _redis->get(k);            // 获取用户信息
            if (v)
            {
                Json::Value u;
                if (json_util::unserialize(*v, u) && !u["username"].isNull()) // 反序列化用户信息
                    _redis->del(keyByName(u["username"].asString()));         // 删除name-key
            }
            _redis->del(k); // 删除id-key
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed);
            ERR_LOG("redis invalidateById failed: %s", e.what());
            return false;
        }
    }

    // 删除用户信息
    // invalidateByName 的做法是：
    // 1.先读 name-key
    // 2.从 value 里拿 uid
    // 3.删 id-key
    // 4.再删 name-key
    bool invalidateByName(const std::string &name) override
    {
        try
        {
            const std::string k = keyByName(name); // 生成用户名称键
            auto v = _redis->get(k);               // 获取用户信息
            if (v)
            {
                Json::Value u;
                if (json_util::unserialize(*v, u) && !u["id"].isNull()) // 反序列化用户信息
                    _redis->del(keyById(u["id"].asUInt64()));           // 删除id-key

                _redis->del(k); // 删除name-key
                return true;
            }
            return false;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed);
            ERR_LOG("redis invalidateByName failed: %s", e.what());
            return false;
        }
    }

private:
    // 获取用户信息
    bool getByKey(const std::string &k, Json::Value &user)
    {
        try
        {
            auto v = _redis->get(k); // 获取用户信息
            if (!v)
            {
                cache_metrics::redis_miss_total.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (!json_util::unserialize(*v, user)) // 反序列化用户信息
            {
                cache_metrics::redis_miss_total.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            cache_metrics::redis_hits_total.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed);
            ERR_LOG("redis getUser failed: %s", e.what());
            return false;
        }
    }

    // 生成用户ID键
    static std::string keyById(uint64_t uid) { return std::string("user:id:") + std::to_string(uid); }
    // 生成用户名称键
    static std::string keyByName(const std::string &name) { return std::string("user:name:") + name; }

private:
    std::shared_ptr<sw::redis::Redis> _redis;
};
