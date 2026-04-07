#pragma once

#include <cstdint>
#include <string>
// 定义了 4 个会话操作
class SessionCache
{
public:
    virtual ~SessionCache() = default;
    virtual bool setSession(uint64_t ssid, uint64_t uid, int ttlSec) = 0;
    virtual bool getSession(uint64_t ssid, uint64_t &uid) = 0;
    virtual bool delSession(uint64_t ssid) = 0;
    virtual bool expireSession(uint64_t ssid, int ttlSec) = 0;
};
// 提供 NoopSessionCache（全返回 false）
// 用途：Redis 不可用时的降级实现，避免程序崩溃。
class NoopSessionCache : public SessionCache
{
public:
    bool setSession(uint64_t, uint64_t, int) override { return false; }
    bool getSession(uint64_t, uint64_t &) override { return false; }
    bool delSession(uint64_t) override { return false; }
    bool expireSession(uint64_t, int) override { return false; }
};
