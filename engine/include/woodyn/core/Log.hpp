#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace woodyn::log {

enum class Level { Trace, Debug, Info, Warn, Error };

/// Initializes the global logger (console sink). Safe to call more than once.
void init(Level level = Level::Info);

void setLevel(Level level);

/// Writes an already formatted message.
void write(Level level, std::string_view message);

template <typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace woodyn::log
