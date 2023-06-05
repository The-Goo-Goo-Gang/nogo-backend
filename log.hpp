#pragma once

#include <iostream>
#include <optional>

#include <magic_enum.hpp>
#include <magic_enum_format.hpp>

#include "spdlog/async.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

std::optional<spdlog::logger> logger;
void init_log()
{
    auto console_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    auto trace_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("./logs/trace_log", true);
    trace_sink->set_level(spdlog::level::trace);
    auto debug_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("./logs/debug_log", true);
    debug_sink->set_level(spdlog::level::debug);
    auto info_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("./logs/info_log", true);
    info_sink->set_level(spdlog::level::info);
    auto warn_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("./logs/warn_log", true);
    warn_sink->set_level(spdlog::level::warn);
    logger = spdlog::logger("logger", { console_sink, trace_sink, debug_sink, info_sink, warn_sink });
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::err);
}