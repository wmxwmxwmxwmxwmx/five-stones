#pragma once

#include <atomic>
// Redis 实现里每次命中/miss/异常都会更新它们
namespace cache_metrics
{
    inline std::atomic<uint64_t> redis_hits_total{0};   // Redis命中计数
    inline std::atomic<uint64_t> redis_miss_total{0};   // Redis未命中计数
    inline std::atomic<uint64_t> redis_errors_total{0}; // Redis错误计数
}
