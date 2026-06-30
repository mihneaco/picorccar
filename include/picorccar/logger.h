#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include "pico/stdio.h"
#include "ulog.h"

namespace logger
{
/**
 * @brief uLog sink that writes to pico stdio.
 * @note Call after stdio_init_all(); uLog uses shared static storage, so avoid
 *       logging from IRQ paths.
 */
inline void pico_stdio_sink(const ulog_level_t severity, char* const message)
{
#ifdef ULOG_ENABLED
    stdio_printf("[%s] %s\n", ulog_level_name(severity), message);
#else
    (void)severity;
    (void)message;
#endif
}

inline ulog_err_t init(const ulog_level_t threshold = ULOG_INFO_LEVEL)
{
#ifdef ULOG_ENABLED
    ulog_init();
    return ulog_subscribe(pico_stdio_sink, threshold);
#else
    (void)threshold;
    return ULOG_ERR_NONE;
#endif
}

/// @brief Function-entry log overload used by LOG_INFO() and friends with no message.
inline void log(const ulog_level_t severity, const char* const function)
{
#ifdef ULOG_ENABLED
    ulog_message(severity, "%s", function);
#else
    (void)severity;
    (void)function;
#endif
}

/// @brief Function-prefixed message overload used by LOG_INFO("...", args...) and friends.
inline void log(const ulog_level_t severity, const char* const function, const char* const format, ...)
{
#ifdef ULOG_ENABLED
    char message[ULOG_MAX_MESSAGE_LENGTH];
    int prefix_length = std::snprintf(message, sizeof(message), "%s: ", function);
    if (prefix_length < 0)
        return;
    if (static_cast<std::size_t>(prefix_length) >= sizeof(message))
        prefix_length = sizeof(message) - 1;

    va_list args;
    va_start(args, format);
    std::vsnprintf(&message[prefix_length], sizeof(message) - static_cast<std::size_t>(prefix_length), format, args);
    va_end(args);

    ulog_message(severity, "%s", message);
#else
    (void)severity;
    (void)function;
    (void)format;
#endif
}
}

#define LOG_TRACE(...)    logger::log(ULOG_TRACE_LEVEL,    __func__ __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(...)    logger::log(ULOG_DEBUG_LEVEL,    __func__ __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(...)     logger::log(ULOG_INFO_LEVEL,     __func__ __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARNING(...)  logger::log(ULOG_WARNING_LEVEL,  __func__ __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(...)    logger::log(ULOG_ERROR_LEVEL,    __func__ __VA_OPT__(,) __VA_ARGS__)
#define LOG_CRITICAL(...) logger::log(ULOG_CRITICAL_LEVEL, __func__ __VA_OPT__(,) __VA_ARGS__)
#define LOG_ALWAYS(...)   logger::log(ULOG_ALWAYS_LEVEL,   __func__ __VA_OPT__(,) __VA_ARGS__)
