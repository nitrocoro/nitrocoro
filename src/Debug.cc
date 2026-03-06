/**
 * @file Debug.cc
 * @brief Debug utilities implementation
 */
#include <nitrocoro/utils/Debug.h>

#include <cstdlib>
#include <cstring>

namespace nitrocoro
{

static LogLevel parseLogLevel(const char * str)
{
    if (!str)
        return LogLevel::Info;
    if (strcmp(str, "TRACE") == 0 || strcmp(str, "trace") == 0)
        return LogLevel::Trace;
    if (strcmp(str, "DEBUG") == 0 || strcmp(str, "debug") == 0)
        return LogLevel::Debug;
    if (strcmp(str, "INFO") == 0 || strcmp(str, "info") == 0)
        return LogLevel::Info;
    if (strcmp(str, "ERROR") == 0 || strcmp(str, "error") == 0)
        return LogLevel::Error;
    if (strcmp(str, "OFF") == 0 || strcmp(str, "off") == 0)
        return LogLevel::Off;
    return LogLevel::Info;
}

static LogLevel g_logLevel = parseLogLevel(std::getenv("NITROCORO_LOG_LEVEL"));

void setLogLevel(LogLevel level)
{
    g_logLevel = level;
}

LogLevel getLogLevel()
{
    return g_logLevel;
}

} // namespace nitrocoro
