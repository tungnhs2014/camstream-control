#ifndef CAMSTREAM_LOGGING_HPP
#define CAMSTREAM_LOGGING_HPP

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ios>
#include <string>
#include <string_view>
#include <type_traits>

namespace camstream::logging {

enum class Level {
    Error,
    Warning,
    Info,
    Debug,
};

inline const char* level_name(Level level) noexcept {
    switch (level) {
    case Level::Error:
        return "ERROR";
    case Level::Warning:
        return "WARN";
    case Level::Info:
        return "INFO";
    case Level::Debug:
        return "DEBUG";
    }
    return "UNKNOWN";
}

inline const char* base_name(const char* path) noexcept {
    if (path == nullptr) {
        return "unknown";
    }

    const char* name = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            name = cursor + 1;
        }
    }
    return name;
}

/**
 * @brief Builds and emits one bounded diagnostic line without dynamic allocation or exceptions.
 * @note Restores the caller's errno before message evaluation and after every logger operation.
 */
class LogLine final {
  public:
    LogLine(Level level, const char* file, int line, int caller_errno) noexcept : saved_errno(caller_errno) {
        append("[");
        append(level_name(level));
        append("] ");
        append(base_name(file));
        append(":");
        append_integer(line);
        append(": ");
        restore_errno();
    }

    ~LogLine() noexcept {
        if (length == 0U || buffer[length - 1U] != '\n') {
            buffer[length++] = '\n';
        }
        static_cast<void>(std::fwrite(buffer, 1U, length, stderr));
        static_cast<void>(std::fflush(stderr));
        restore_errno();
    }

    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    LogLine(LogLine&&) = delete;
    LogLine& operator=(LogLine&&) = delete;

    LogLine& stream() noexcept {
        return *this;
    }

    LogLine& operator<<(const char* value) noexcept {
        append(value != nullptr ? value : "(null)");
        restore_errno();
        return *this;
    }

    LogLine& operator<<(char value) noexcept {
        append(&value, 1U);
        restore_errno();
        return *this;
    }

    LogLine& operator<<(const std::string& value) noexcept {
        append(value.data(), value.size());
        restore_errno();
        return *this;
    }

    LogLine& operator<<(std::string_view value) noexcept {
        append(value.data(), value.size());
        restore_errno();
        return *this;
    }

    LogLine& operator<<(bool value) noexcept {
        append(value ? "true" : "false");
        restore_errno();
        return *this;
    }

    template <typename Integer, std::enable_if_t<std::is_integral_v<Integer> && !std::is_same_v<Integer, bool> &&
                                                     !std::is_same_v<Integer, char>,
                                                 int> = 0>
    LogLine& operator<<(Integer value) noexcept {
        append_integer(value);
        restore_errno();
        return *this;
    }

    template <typename Enumeration, std::enable_if_t<std::is_enum_v<Enumeration>, int> = 0>
    LogLine& operator<<(Enumeration value) noexcept {
        append_integer(static_cast<std::underlying_type_t<Enumeration>>(value));
        restore_errno();
        return *this;
    }

    template <typename Floating, std::enable_if_t<std::is_floating_point_v<Floating>, int> = 0>
    LogLine& operator<<(Floating value) noexcept {
        char text[64]{};
        const int result = std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
        if (result > 0) {
            append_formatted(text, sizeof(text), result);
        }
        restore_errno();
        return *this;
    }

    LogLine& operator<<(const void* value) noexcept {
        char text[32]{};
        const int result = std::snprintf(text, sizeof(text), "%p", value);
        if (result > 0) {
            append_formatted(text, sizeof(text), result);
        }
        restore_errno();
        return *this;
    }

    using BaseManipulator = std::ios_base& (*)(std::ios_base&);

    LogLine& operator<<(BaseManipulator manipulator) noexcept {
        if (manipulator == static_cast<BaseManipulator>(std::hex)) {
            integer_base = 16;
        } else if (manipulator == static_cast<BaseManipulator>(std::dec)) {
            integer_base = 10;
        }
        restore_errno();
        return *this;
    }

  private:
    static constexpr std::size_t kBufferSize = 1024U;

    void restore_errno() const noexcept {
        errno = saved_errno;
    }

    void append(const char* value) noexcept {
        append(value, value != nullptr ? std::strlen(value) : 0U);
    }

    void append(const char* value, std::size_t value_length) noexcept {
        if (value == nullptr || value_length == 0U || length >= kBufferSize - 1U) {
            return;
        }
        const std::size_t available = kBufferSize - 1U - length;
        const std::size_t copy_length = value_length < available ? value_length : available;
        std::memcpy(buffer + length, value, copy_length);
        length += copy_length;
    }

    void append_formatted(const char* value, std::size_t capacity, int formatted_length) noexcept {
        if (value == nullptr || capacity == 0U || formatted_length <= 0) {
            return;
        }
        const std::size_t reported_length = static_cast<std::size_t>(formatted_length);
        append(value, reported_length < capacity ? reported_length : capacity - 1U);
    }

    template <typename Integer> void append_integer(Integer value) noexcept {
        char text[64]{};
        int result = 0;
        if (integer_base == 16) {
            result = std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(value));
        } else if constexpr (std::is_signed_v<Integer>) {
            result = std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
        } else {
            result = std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
        }
        if (result > 0) {
            append_formatted(text, sizeof(text), result);
        }
    }

    char buffer[kBufferSize]{};
    std::size_t length = 0U;
    int integer_base = 10;
    const int saved_errno;
};

} // namespace camstream::logging

#define CAMSTREAM_LOG(level, message)                                                                                  \
    do {                                                                                                               \
        const int camstream_log_errno_snapshot = errno;                                                                \
        static_cast<void>(                                                                                             \
            ::camstream::logging::LogLine((level), __FILE__, __LINE__, camstream_log_errno_snapshot).stream()          \
            << message);                                                                                               \
    } while (false)

#define LOGE(message) CAMSTREAM_LOG(::camstream::logging::Level::Error, message)
#define LOGW(message) CAMSTREAM_LOG(::camstream::logging::Level::Warning, message)
#define LOGI(message) CAMSTREAM_LOG(::camstream::logging::Level::Info, message)
#define LOGD(message) CAMSTREAM_LOG(::camstream::logging::Level::Debug, message)

#endif
