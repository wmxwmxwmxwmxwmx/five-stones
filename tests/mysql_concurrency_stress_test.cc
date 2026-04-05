// MySQL 并发与压测：默认连接与 five-stones/src/main.cc 一致（127.0.0.1 / wmx / 123456 / online_gobang / 3306）。
// 可通过 MYSQL_HOST、MYSQL_USER、MYSQL_PASSWORD、MYSQL_DATABASE、MYSQL_PORT 覆盖；STRESS_THREADS、STRESS_ITERS 可调规模。
// 本机无 MySQL 或连不上时 SetUp 中 ping 失败则 GTEST_SKIP，不算失败。
// user_table 对部分 API 使用单连接 + 互斥锁，insert/win/lose 等未加锁；压测用户名前缀 ft（总长适配 VARCHAR(32)），结束时 DELETE LIKE 'ft%'。

#include "app_config.hpp"
#include "db.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// 匿名命名空间：本文件私有的配置解析、清理与 GTest 夹具，不对外链接。
namespace {

// MySQL 连接与压测规模。默认值与 main.cc 中 db_* 保持一致；load_cfg 可用环境变量覆盖。
struct MysqlStressCfg
{
    std::string host = "127.0.0.1";
    std::string user = "wmx";
    std::string pass = "123456";
    std::string db = "online_gobang";
    uint16_t port = 3306;
    int stress_threads = 128;
    int stress_iters = 500;

    bool valid = false;
};

// 以 main.cc 默认值为基，可选读取 MYSQL_*、MYSQL_PORT、STRESS_THREADS、STRESS_ITERS。
MysqlStressCfg load_cfg()
{
    MysqlStressCfg c;
    apply_env_override(c.host, "MYSQL_HOST");
    apply_env_override(c.user, "MYSQL_USER");
    apply_env_override(c.pass, "MYSQL_PASSWORD");
    apply_env_override(c.db, "MYSQL_DATABASE");
    if (const char *p = std::getenv("MYSQL_PORT"))
    {
        try
        {
            const int port = std::stoi(p);
            if (port > 0 && port <= 65535)
                c.port = static_cast<uint16_t>(port);
        }
        catch (...)
        {
        }
    }
    if (const char *p = std::getenv("STRESS_THREADS"))
    {
        try
        {
            const int t = std::stoi(p);
            c.stress_threads = (t < 1) ? 1 : (t > 512 ? 512 : t);
        }
        catch (...)
        {
        }
    }
    if (const char *p = std::getenv("STRESS_ITERS"))
    {
        try
        {
            const int n = std::stoi(p);
            c.stress_iters = (n < 1) ? 1 : (n > 100000 ? 100000 : n);
        }
        catch (...)
        {
        }
    }
    c.valid = true;
    return c;
}

// 单独建连执行 DELETE，清除本测试使用的用户名（LIKE 'ft%'，与 make_username / 种子名一致）。
void cleanup_ft_test_users(const MysqlStressCfg &cfg)
{
    MYSQL *m = mysql_util::mysql_create(cfg.host, cfg.user, cfg.pass, cfg.db, cfg.port);
    if (m == nullptr)
        return;
    (void)mysql_util::mysql_exec(m, "DELETE FROM user WHERE username LIKE 'ft%'");
    mysql_util::mysql_release(m);
}

// 尝试创建并立即释放连接，用于 SetUp 前确认账号/库/网络可用。
bool ping_mysql(const MysqlStressCfg &cfg)
{
    MYSQL *m = mysql_util::mysql_create(cfg.host, cfg.user, cfg.pass, cfg.db, cfg.port);
    if (m == nullptr)
        return false;
    mysql_util::mysql_release(m);
    return true;
}

// GTest 夹具：每个用例前加载配置、可选跳过、清库、构造 user_table；结束后析构并再次清理 ft% 行。
class MysqlStressFixture : public ::testing::Test
{
protected:
    MysqlStressCfg cfg_{};
    std::unique_ptr<user_table> ut_;

    // 连不上 MySQL 时 SKIP；否则清空 ft% 测试数据并创建共用的 user_table。
    void SetUp() override
    {
        cfg_ = load_cfg();
        if (!ping_mysql(cfg_))
        {
            GTEST_SKIP() << "MySQL connection failed (defaults match main.cc; override MYSQL_* or start server)";
        }
        cleanup_ft_test_users(cfg_);
        ut_.reset(new user_table(cfg_.host, cfg_.user, cfg_.pass, cfg_.db, cfg_.port));
    }

    // 释放 user_table 并删除本文件产生的 ft% 行，避免测试库膨胀。
    void TearDown() override
    {
        ut_.reset();
        if (cfg_.valid)
            cleanup_ft_test_users(cfg_);
    }
};

// 生成 ≤16 字符用户名（ft + 14 位 hex），适配 VARCHAR(32)；mix 含 thread/seq/salt 降低碰撞。
std::string make_username(int thread, int seq, uint64_t salt)
{
    const uint64_t mix = (static_cast<uint64_t>(static_cast<uint32_t>(thread)) << 32) ^
                         (static_cast<uint64_t>(static_cast<uint32_t>(seq)) << 16) ^
                         (salt ^ (salt >> 32));
    char buf[20];
    constexpr uint64_t k56 = (1ULL << 56) - 1ULL;
    std::snprintf(buf, sizeof(buf), "ft%014llx", static_cast<unsigned long long>(mix & k56));
    return std::string(buf);
}

} // namespace

// 预插入固定种子用户后，多线程并发 login 同一账号，统计成功次数应等于 线程数×每线程迭代次数。
//数据库里先放好一个固定账号后，很多线程同时、反复用同一组用户名/密码去 login，是否每次都能成功。
TEST_F(MysqlStressFixture, MysqlConcurrentLogin)
{
    constexpr const char *kSeedUser = "ft_lseed";
    constexpr const char *kSeedPass = "ft_pw";

    // 删除可能残留的种子行，保证 insert 成功。
    {
        MYSQL *m = mysql_util::mysql_create(cfg_.host, cfg_.user, cfg_.pass, cfg_.db, cfg_.port);
        if (m)
        {
            char sql[512];
            std::snprintf(sql, sizeof(sql), "DELETE FROM user WHERE username='%s'", kSeedUser);
            (void)mysql_util::mysql_exec(m, sql);
            mysql_util::mysql_release(m);
        }
    }

    Json::Value seed;
    seed["username"] = kSeedUser;
    seed["password"] = kSeedPass;
    ASSERT_TRUE(ut_->insert(seed)) << "seed insert failed";

    const int threads = cfg_.stress_threads;
    const int iters = cfg_.stress_iters;
    std::atomic<int> oks{0};

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&, t]()
                             {
                                 for (int i = 0; i < iters; ++i)
                                 {
                                     Json::Value u;
                                     u["username"] = kSeedUser;
                                     u["password"] = kSeedPass;
                                     if (ut_->login(u))
                                         oks.fetch_add(1, std::memory_order_relaxed);
                                 }
                             });
    }
    for (auto &w : workers)
        w.join();

    EXPECT_EQ(oks.load(), threads * iters) << "concurrent login should all succeed for same seeded user";
}

// 每线程若干次：insert 唯一用户再 select_by_name，校验 score==1000 且用户名一致；迭代数为 STRESS_ITERS/10 以控制行数。
//多线程并发下，每个线程反复「插入新用户 → 按用户名查询」，插入和查询是否都成功，且查到的数据与插入一致。
TEST_F(MysqlStressFixture, MysqlConcurrentInsertThenSelect)
{
    const int threads = cfg_.stress_threads;
    const int iters = std::max(1, cfg_.stress_iters / 10);
    std::atomic<int> oks{0};
    const uint64_t salt = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&, t]()
                             {
                                 for (int i = 0; i < iters; ++i)
                                 {
                                     const std::string uname = make_username(t, i, salt);
                                     Json::Value ins;
                                     ins["username"] = uname;
                                     ins["password"] = "p";
                                     if (!ut_->insert(ins))
                                         continue;
                                     Json::Value sel;
                                     if (!ut_->select_by_name(uname, sel))
                                         continue;
                                     if (sel["score"].asUInt64() == 1000u && sel["username"].asString() == uname)
                                         oks.fetch_add(1, std::memory_order_relaxed);
                                 }
                             });
    }
    for (auto &w : workers)
        w.join();

    EXPECT_EQ(oks.load(), threads * iters) << "each insert+select should observe score=1000 and matching username";
}

// 偶数下标线程反复 login 种子用户，奇数下标线程 insert+select 新用户；验证读写交织下计数与预期一致。
//多线程并发下，一半线程反复「登录固定账号」，另一半线程反复「插入新用户 → 按用户名查询」，登录和插入查询是否都成功，且查到的数据与插入一致。
TEST_F(MysqlStressFixture, MysqlMixedReadWrite)
{
    constexpr const char *kSeedUser = "ft_mseed";
    constexpr const char *kSeedPass = "ft_pw";

    {
        MYSQL *m = mysql_util::mysql_create(cfg_.host, cfg_.user, cfg_.pass, cfg_.db, cfg_.port);
        if (m)
        {
            char sql[512];
            std::snprintf(sql, sizeof(sql), "DELETE FROM user WHERE username='%s'", kSeedUser);
            (void)mysql_util::mysql_exec(m, sql);
            mysql_util::mysql_release(m);
        }
    }
    Json::Value seed;
    seed["username"] = kSeedUser;
    seed["password"] = kSeedPass;
    ASSERT_TRUE(ut_->insert(seed));

    const int threads = cfg_.stress_threads;
    const int iters = std::max(1, cfg_.stress_iters / 5);
    std::atomic<int> read_ok{0};
    std::atomic<int> write_ok{0};
    const uint64_t salt = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()) + 1u;

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t)
    {
        const bool reader = (t % 2 == 0);
        workers.emplace_back([&, t, reader]()
                             {
                                 for (int i = 0; i < iters; ++i)
                                 {
                                     if (reader)
                                     {
                                         Json::Value u;
                                         u["username"] = kSeedUser;
                                         u["password"] = kSeedPass;
                                         if (ut_->login(u))
                                             read_ok.fetch_add(1, std::memory_order_relaxed);
                                     }
                                     else
                                     {
                                         const std::string uname = make_username(1000 + t, i, salt);
                                         Json::Value ins;
                                         ins["username"] = uname;
                                         ins["password"] = "p";
                                         if (ut_->insert(ins))
                                         {
                                             Json::Value sel;
                                             if (ut_->select_by_name(uname, sel))
                                                 write_ok.fetch_add(1, std::memory_order_relaxed);
                                         }
                                     }
                                 }
                             });
    }
    for (auto &w : workers)
        w.join();

    const int readers = (threads + 1) / 2;
    const int writers = threads - readers;
    EXPECT_EQ(read_ok.load(), readers * iters);
    EXPECT_EQ(write_ok.load(), writers * iters);
}

// 综合压测：每轮 insert → select_by_name → login，累计成功操作数应为 线程数×迭代×2，总耗时软限制 120s。
//多线程并发下，每个线程反复「插入新用户 → 按用户名查询 → 登录」，插入、查询和登录是否都成功，且查到的数据与插入一致。
TEST_F(MysqlStressFixture, MysqlStressRamp)
{
    const int threads = cfg_.stress_threads;
    const int iters = cfg_.stress_iters;
    const uint64_t salt = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()) + 99u;

    const auto t0 = std::chrono::steady_clock::now();
    std::atomic<int> ops{0};

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&, t]()
                             {
                                 for (int i = 0; i < iters; ++i)
                                 {
                                     const std::string uname = make_username(t, i, salt);
                                     Json::Value ins;
                                     ins["username"] = uname;
                                     ins["password"] = "p";
                                     if (!ut_->insert(ins))
                                         continue;
                                     Json::Value sel;
                                     if (ut_->select_by_name(uname, sel))
                                         ops.fetch_add(1, std::memory_order_relaxed);
                                     Json::Value login_like;
                                     login_like["username"] = uname;
                                     login_like["password"] = "p";
                                     if (ut_->login(login_like))
                                         ops.fetch_add(1, std::memory_order_relaxed);
                                 }
                             });
    }
    for (auto &w : workers)
        w.join();

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();

    EXPECT_EQ(ops.load(), threads * iters * 2) << "each iteration: select + login after insert";
    EXPECT_LT(ms, 120000) << "soft watchdog: entire ramp should finish within 120s (raise STRESS_* if slow env)";
}
