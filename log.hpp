#pragma once
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG

#include <iostream>
#include <vector>
#include <optional>
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"

std::optional<spdlog::logger> logger;
void init_log(){
    auto trace_sink = std::make_shared<spdlog::sinks::rotating_file_sink_st>("../logs/trace_log", 1024*1024*5,20, true);
    trace_sink->set_level(spdlog::level::trace);
    auto debug_sink = std::make_shared<spdlog::sinks::rotating_file_sink_st>("../logs/debug_log", 1024*1024*5,20, true);
    debug_sink->set_level(spdlog::level::debug);
    auto info_sink = std::make_shared<spdlog::sinks::basic_file_sink_st>("../logs/info_log", true);
    info_sink->set_level(spdlog::level::info);
    auto warn_sink = std::make_shared<spdlog::sinks::basic_file_sink_st>("../logs/warn_log", true);
    warn_sink->set_level(spdlog::level::warn);
    std::vector<spdlog::sink_ptr> sinks = { trace_sink, debug_sink, info_sink, warn_sink };
    logger = spdlog::logger("logger", sinks.begin(), sinks.end());
    logger->flush_on(spdlog::level::err);
}