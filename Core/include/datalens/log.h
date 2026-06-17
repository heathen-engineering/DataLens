/******************************************************************************
 * log.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Pluggable logging seam for the engine-agnostic DataLens core. The core never
 * depends on any engine's logging. By default messages go to stderr; a host
 * (Unity, Unreal, O3DE, Godot) can route them to its own logger via
 * SetLogCallback.
 ******************************************************************************/

#pragma once

namespace datalens
{
    enum class LogLevel
    {
        Trace,
        Info,
        Warning,
        Error
    };

    using LogFn = void (*)(LogLevel level, const char* message);

    /// Install a host log sink. Pass nullptr to restore the default stderr sink.
    void SetLogCallback(LogFn fn);

    /// Emit a log message through the current sink.
    void Log(LogLevel level, const char* message);
}

#define DATALENS_LOG(level, msg) ::datalens::Log(::datalens::LogLevel::level, (msg))
