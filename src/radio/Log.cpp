#include "Log.h"

#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <time.h>

namespace winject
{

namespace
{
std::mutex g_log_mu;
LogLevel g_level = LogLevel::info;
}  // namespace

void log_set_level(LogLevel level)
{
    g_level = level;
}

LogLevel log_get_level()
{
    return g_level;
}

void log_printf(LogLevel level, const char* fmt, ...)
{
    if (static_cast<int>(level) > static_cast<int>(g_level))
    {
        return;
    }
    const char* tag = "INF";
    switch (level)
    {
    case LogLevel::error:
        tag = "ERR";
        break;
    case LogLevel::warn:
        tag = "WRN";
        break;
    case LogLevel::info:
        tag = "INF";
        break;
    case LogLevel::debug:
        tag = "DBG";
        break;
    }
    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm = {};
    localtime_r(&ts.tv_sec, &tm);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%04d-%02d-%02d %02d:%02d:%02d.%03ld | %s | ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, tag);
    std::lock_guard<std::mutex> lock(g_log_mu);
    fputs(prefix, stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

}  // namespace winject
