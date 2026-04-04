#include "match_queue.hpp"
#include "session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Concurrency / stress: match_queue

// 8 个生产者线程各 push 500 次，主线程再全部 pop；元素总数正确、排序后相邻无重复（无重复弹出）。
TEST(Concurrency, MatchQueueManyProducersSingleConsumer)
{
    match_queue<uint64_t> q;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(kThreads));
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&, t]()
                             {
                                 const int base = t * 1'000'000;
                                 for (int i = 0; i < kPerThread; ++i)
                                     q.push(static_cast<uint64_t>(base + i));
                             });
    }
    for (auto &w : workers)
        w.join();

    EXPECT_EQ(q.size(), kThreads * kPerThread);
    std::vector<uint64_t> popped;
    popped.reserve(static_cast<size_t>(kThreads * kPerThread));
    uint64_t v = 0;
    while (q.pop(v))
        popped.push_back(v);
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(static_cast<int>(popped.size()), kThreads * kPerThread);
    std::sort(popped.begin(), popped.end());
    for (size_t i = 1; i < popped.size(); ++i)
        EXPECT_NE(popped[i], popped[i - 1]) << "duplicate pop";
}

// 多轮：wait 线程在对方 push 后被唤醒；每轮可 pop 出两个元素且队列最终为空。
TEST(Concurrency, MatchQueueWaitUnblocksRepeatedly)
{
    match_queue<uint64_t> q;
    constexpr int kRounds = 40;
    for (int r = 0; r < kRounds; ++r)
    {
        std::atomic<bool> started{false};
        std::thread waiter([&]()
                           {
                               started = true;
                               q.wait();
                           });
        while (!started)
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        q.push(1);
        q.push(2);
        waiter.join();
        uint64_t a = 0, b = 0;
        ASSERT_TRUE(q.pop(a));
        ASSERT_TRUE(q.pop(b));
        EXPECT_EQ(q.size(), 0);
    }
}

// 单线程大量交替 push/pop（20 万次）的正确性，并断言总耗时小于 30s；默认 DISABLED 不跑。
TEST(Stress, DISABLED_MatchQueuePushPopSingleThreaded)
{
    match_queue<uint64_t> q;
    constexpr int kN = 200'000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kN; ++i)
    {
        q.push(static_cast<uint64_t>(i));
        uint64_t v = 0;
        ASSERT_TRUE(q.pop(v));
        EXPECT_EQ(v, static_cast<uint64_t>(i));
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    EXPECT_LT(ms, 30000);
}

// Concurrency / stress: session_manager (no set_session_expire_time)

// 16 线程各创建 125 个 session，收集全部 ssid 后应无重复（并发下 ssid 全局唯一）。
TEST(Concurrency, SessionManagerConcurrentCreateUniqueSsids)
{
    session_manager sm(nullptr);
    constexpr int kThreads = 16;
    constexpr int kEach = 125;
    std::mutex mu;
    std::vector<uint64_t> ssids;
    ssids.reserve(static_cast<size_t>(kThreads * kEach));
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&]()
                             {
                                 for (int i = 0; i < kEach; ++i)
                                 {
                                     auto sp = sm.create_session(1000u, LOGIN);
                                     ASSERT_TRUE(sp);
                                     std::lock_guard<std::mutex> lock(mu);
                                     ssids.push_back(sp->ssid());
                                 }
                             });
    }
    for (auto &w : workers)
        w.join();
    EXPECT_EQ(ssids.size(), static_cast<size_t>(kThreads * kEach));
    std::sort(ssids.begin(), ssids.end());
    auto last = std::unique(ssids.begin(), ssids.end());
    EXPECT_EQ(last, ssids.end()) << "duplicate ssid";
}

// 先单线程创建 200 个 session；8 个读线程用原子计数做屏障同步后并发 get_session_by_ssid，ssid 与查询值一致。
TEST(Concurrency, SessionManagerConcurrentReadAfterBarrier)
{
    session_manager sm(nullptr);
    std::vector<uint64_t> ids;
    constexpr int kN = 200;
    for (int i = 0; i < kN; ++i)
    {
        auto sp = sm.create_session(static_cast<uint64_t>(i), LOGIN);
        ids.push_back(sp->ssid());
    }
    std::atomic<int> ready{0};
    constexpr int kReaders = 8;
    std::vector<std::thread> readers;
    for (int r = 0; r < kReaders; ++r)
    {
        readers.emplace_back([&]()
                             {
                                 ready.fetch_add(1);
                                 while (ready.load() < kReaders)
                                     std::this_thread::yield();
                                 for (uint64_t sid : ids)
                                 {
                                     auto sp = sm.get_session_by_ssid(sid);
                                     ASSERT_TRUE(sp);
                                     EXPECT_EQ(sp->ssid(), sid);
                                 }
                             });
    }
    for (auto &th : readers)
        th.join();
}
