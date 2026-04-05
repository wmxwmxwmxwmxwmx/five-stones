// 日志：严重度 DEBUG(0) < INFO(1) < ERROR(2)；仅当 msg_severity >= 最低输出级别时打印。
// 环境变量 LOG_LEVEL：debug（默认）/ info / error（不区分大小写）。生产建议 LOG_LEVEL=error。
// ERROR 写入 stderr，DEBUG/INFO 写入 stdout。

#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>

namespace log_detail
{

constexpr int kSevDebug = 0;
constexpr int kSevInfo = 1;
constexpr int kSevError = 2;

inline int min_severity_from_env()
{
    const char *v = std::getenv("LOG_LEVEL");
    if (v == nullptr || v[0] == '\0')
        return kSevDebug;

    char buf[32];
    size_t n = 0;
    for (; v[n] != '\0' && n + 1 < sizeof(buf); ++n)
        buf[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(v[n])));
    buf[n] = '\0';

    if (std::strcmp(buf, "error") == 0)
        return kSevError;
    if (std::strcmp(buf, "info") == 0)
        return kSevInfo;
    if (std::strcmp(buf, "debug") == 0)
        return kSevDebug;
    return kSevDebug;
}

inline int log_min_severity()
{
    // C++11：函数内 static 初始化只执行一次且线程安全。
    static const int k_min = min_severity_from_env();
    return k_min;
}

inline void format_time_iso8601_local(char *buf, size_t buflen)
{
    const time_t t = time(nullptr);
    struct tm tm_buf;
    struct tm *const lt = localtime_r(&t, &tm_buf);
    if (lt == nullptr || strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", lt) == 0u)
        buf[0] = '\0';
}

} // namespace log_detail

#define LOG_INTERNAL(severity, level_tag, format, ...)                                                                       \
    do                                                                                                                       \
    {                                                                                                                        \
        if ((severity) < log_detail::log_min_severity())                                                                     \
            break;                                                                                                           \
        char log_ts_[40];                                                                                                    \
        log_detail::format_time_iso8601_local(log_ts_, sizeof(log_ts_));                                                   \
        FILE *const log_out_ = ((severity) >= log_detail::kSevError) ? stderr : stdout;                                      \
        std::fprintf(log_out_, "%s [%s] [%p] [%s:%d] " format "\n", (level_tag), log_ts_, (void *)pthread_self(), __FILE__,   \
                     __LINE__, ##__VA_ARGS__);                                                                               \
    } while (0)

#define DBG_LOG(format, ...) LOG_INTERNAL(log_detail::kSevDebug, "[DBG]", format, ##__VA_ARGS__)
#define INF_LOG(format, ...) LOG_INTERNAL(log_detail::kSevInfo, "[INF]", format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG_INTERNAL(log_detail::kSevError, "[ERR]", format, ##__VA_ARGS__)
