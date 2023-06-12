#pragma once

#include <iostream>
#include <optional>

#include "spdlog/async.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

std::optional<spdlog::logger> logger;
std::optional<spdlog::logger> alphazero_logger;
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
    logger->flush_on(spdlog::level::debug);

    auto alphazero_console_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
    alphazero_console_sink->set_level(spdlog::level::info);
    auto alphazero_info_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("./logs/alphazero_info_log", 1024 * 1024 * 5, 20);
    alphazero_info_sink->set_level(spdlog::level::info);
    auto alphazero_warn_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("./logs/alphazero_warn_log", 1024 * 1024 * 5, 20);
    alphazero_warn_sink->set_level(spdlog::level::warn);
    alphazero_logger = spdlog::logger("alphazero_logger", { alphazero_console_sink, alphazero_info_sink, alphazero_warn_sink });
    alphazero_logger->set_level(spdlog::level::info);
    alphazero_logger->flush_on(spdlog::level::info);
}