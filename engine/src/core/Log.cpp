#include "woodyn/core/Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>

namespace woodyn::log {
namespace {

spdlog::level::level_enum toSpdlog(Level level) {
    switch (level) {
    case Level::Trace: return spdlog::level::trace;
    case Level::Debug: return spdlog::level::debug;
    case Level::Info:  return spdlog::level::info;
    case Level::Warn:  return spdlog::level::warn;
    case Level::Error: return spdlog::level::err;
    }
    return spdlog::level::info;
}

std::shared_ptr<spdlog::logger>& logger() {
    static std::shared_ptr<spdlog::logger> instance;
    return instance;
}

std::once_flag gInitFlag;

void ensureInit() {
    std::call_once(gInitFlag, [] {
        auto created = spdlog::stdout_color_mt("woodyn");
        created->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        created->set_level(spdlog::level::info);
        logger() = std::move(created);
    });
}

} // namespace

void init(Level level) {
    ensureInit();
    setLevel(level);
}

void setLevel(Level level) {
    ensureInit();
    logger()->set_level(toSpdlog(level));
}

void write(Level level, std::string_view message) {
    ensureInit();
    logger()->log(toSpdlog(level), "{}", message);
}

} // namespace woodyn::log
