/******************************************************************************
 * log.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 ******************************************************************************/

#include "datalens/log.h"

#include <cstdio>

namespace datalens
{
    namespace
    {
        LogFn g_sink = nullptr;

        const char* LevelName(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:   return "TRACE";
            case LogLevel::Info:    return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error:   return "ERROR";
            }
            return "INFO";
        }
    }

    void SetLogCallback(LogFn fn)
    {
        g_sink = fn;
    }

    void Log(LogLevel level, const char* message)
    {
        if (g_sink)
        {
            g_sink(level, message);
            return;
        }
        std::fprintf(stderr, "[DataLens][%s] %s\n", LevelName(level), message ? message : "");
    }
}
