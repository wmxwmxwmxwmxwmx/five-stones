#pragma once

#include "cache/cache_metrics.hpp"
#include "cache/session_cache.hpp"
#include "logger.hpp"

#include <memory>
#include <string>

#include <sw/redis++/redis++.h>

// RedisSessionCache 实现了 SessionCache 接口，用于将会话信息存储到 Redis 中。
class RedisSessionCache : public SessionCache
{
public:
    explicit RedisSessionCache(const std::shared_ptr<sw::redis::Redis> &redis) : _redis(redis) {}

    // 设置会话信息
    bool setSession(uint64_t ssid, uint64_t uid, int ttlSec) override
    {
        try
        {
            _redis->set(key(ssid), std::to_string(uid)); // 设置会话信息
            if (ttlSec > 0)
                _redis->expire(key(ssid), ttlSec); // 设置会话超时时间
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed); // 记录Redis未命中
            ERR_LOG("redis setSession failed: %s", e.what());                          // 记录错误日志
            return false;
        }
    }

    // 获取会话信息
    bool getSession(uint64_t ssid, uint64_t &uid) override
    {
        try
        {
            auto v = _redis->get(key(ssid)); // 获取会话信息
            if (!v)
            {
                cache_metrics::redis_miss_total.fetch_add(1, std::memory_order_relaxed); // 记录Redis未命中
                return false;
            }
            uid = static_cast<uint64_t>(std::stoull(*v));
            cache_metrics::redis_hits_total.fetch_add(1, std::memory_order_relaxed); // 记录Redis命中
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed); // 记录Redis未命中
            ERR_LOG("redis getSession failed: %s", e.what());
            return false;
        }
    }

    // 删除会话信息
    bool delSession(uint64_t ssid) override
    {
        try
        {
            _redis->del(key(ssid)); // 删除会话信息
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed); // 记录Redis未命中
            ERR_LOG("redis delSession failed: %s", e.what());
            return false;
        }
    }

    // 设置会话超时时间
    bool expireSession(uint64_t ssid, int ttlSec) override
    {
        if (ttlSec <= 0)
            return true;
        try
        {
            _redis->expire(key(ssid), ttlSec); // 设置会话超时时间
            return true;
        }
        catch (const std::exception &e)
        {
            cache_metrics::redis_errors_total.fetch_add(1, std::memory_order_relaxed);
            ERR_LOG("redis expireSession failed: %s", e.what());
            return false;
        }
    }

private:
    // 生成会话键
    static std::string key(uint64_t ssid) { return std::string("sess:") + std::to_string(ssid); }

private:
    std::shared_ptr<sw::redis::Redis> _redis;
};
