#include "match_queue.hpp"
#include "session.hpp"
#include "util.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Boundary / error: json_util

// 空字符串无法反序列化，json_util::unserialize 应返回 false。
TEST(Boundary, JsonUnserializeEmptyString)
{
    Json::Value v;
    EXPECT_FALSE(json_util::unserialize("", v));
}

// 非法 JSON（非 JSON 文本、截断花括号、非法键值结构）均应反序列化失败。
TEST(Boundary, JsonUnserializeInvalid)
{
    Json::Value v;
    EXPECT_FALSE(json_util::unserialize("not json", v));
    EXPECT_FALSE(json_util::unserialize("{", v));
    EXPECT_FALSE(json_util::unserialize("{\"a\":}", v));
}

// Boundary: string_util::split

// 空输入 + 逗号分隔符：按当前实现 split 返回 0 段，out 为空。
TEST(Boundary, StringSplitEmptyInput)
{
    std::vector<std::string> out;
    // 当前实现：while 不进入循环，得到 0 段（与「非空串」行为不同，属边界）
    EXPECT_EQ(string_util::split("", ",", out), 0);
    EXPECT_TRUE(out.empty());
}

// 分隔符为空时，整串视为一段。
TEST(Boundary, StringSplitEmptySeparatorWholeString)
{
    std::vector<std::string> out;
    EXPECT_EQ(string_util::split("abc", "", out), 1);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], "abc");
}

// 输入仅有分隔符（无有效段）时返回 0 段。
TEST(Boundary, StringSplitOnlySeparators)
{
    std::vector<std::string> out;
    EXPECT_EQ(string_util::split(",,", ",", out), 0);
    EXPECT_TRUE(out.empty());
}

// 首尾带分隔符时仍能正确切出中间两段 "a"、"b"。
TEST(Boundary, StringSplitLeadingTrailingSep)
{
    std::vector<std::string> out;
    EXPECT_EQ(string_util::split(",a,b,", ",", out), 2);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], "a");
    EXPECT_EQ(out[1], "b");
}

// Boundary: match_queue

// 空队列上 pop 应返回 false。
TEST(Boundary, MatchQueuePopWhenEmpty)
{
    match_queue<uint64_t> q;
    uint64_t v = 999;
    EXPECT_FALSE(q.pop(v));
}

// remove 不存在的 id 不应抛异常，队列内已有元素不受影响。
TEST(Boundary, MatchQueueRemoveNonexistentNoThrow)
{
    match_queue<uint64_t> q;
    q.push(1);
    q.remove(999u);
    EXPECT_EQ(q.size(), 1);
    uint64_t v = 0;
    ASSERT_TRUE(q.pop(v));
    EXPECT_EQ(v, 1u);
}

// Boundary / error: session_manager

// 不存在的 ssid（0、大数值）查询会话应失败。
TEST(Boundary, SessionManagerGetUnknownSsids)
{
    session_manager sm(nullptr);
    EXPECT_FALSE(sm.get_session_by_ssid(0));
    EXPECT_FALSE(sm.get_session_by_ssid(999999));
}

// 同一 session 连续 remove 第二次不应崩溃；首次 remove 后已查不到该 ssid。
TEST(Boundary, SessionManagerRemoveTwice)
{
    session_manager sm(nullptr);
    auto sp = sm.create_session(42u, LOGIN);
    uint64_t sid = sp->ssid();
    sm.remove_session(sid);
    EXPECT_FALSE(sm.get_session_by_ssid(sid));
    sm.remove_session(sid); // second erase — must not crash
}
