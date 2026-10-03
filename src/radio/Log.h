#ifndef WINJECT_RADIO_LOG_H_
#define WINJECT_RADIO_LOG_H_

namespace winject
{

enum class LogLevel
{
    error,
    warn,
    info,
    debug,
};

void log_set_level(LogLevel level);
LogLevel log_get_level();
void log_printf(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace winject

#define LOG_ERR(fmt, ...) winject::log_printf(winject::LogLevel::error, fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) winject::log_printf(winject::LogLevel::warn, fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...) winject::log_printf(winject::LogLevel::info, fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) winject::log_printf(winject::LogLevel::debug, fmt, ##__VA_ARGS__)

#endif  // WINJECT_RADIO_LOG_H_
