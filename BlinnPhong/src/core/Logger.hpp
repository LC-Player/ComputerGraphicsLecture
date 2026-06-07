#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace RYBlinnPhong {

/**
 * @brief Logging system for the Vulkan application
 *
 * Provides a simple logging system with different log levels
 * and output to both console and file.
 */
class Logger {
public:
    /**
     * @brief Log levels
     */
    enum class Level {
        DEBUG,      ///< Debug information for developers
        INFO,       ///< General information
        WARNING,    ///< Warning messages
        ERR,        ///< Error messages (named ERR to avoid Windows ERROR macro conflict)
        FATAL       ///< Fatal errors that cause application termination
    };

    /**
     * @brief Initialize the logging system
     *
     * @param logFilePath Path to the log file (optional)
     */
    static void init(const std::string& logFilePath = "");

    /**
     * @brief Shutdown the logging system
     */
    static void shutdown();

    /**
     * @brief Set the minimum log level
     *
     * Messages below this level will not be logged.
     *
     * @param level Minimum log level
     */
    static void setMinLevel(Level level);

    /**
     * @brief Log a message with a specific level
     *
     * @param level Log level
     * @param message Message to log
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    static void log(Level level, const std::string& message,
                   const std::string& file = "", int line = 0);

    /**
     * @brief Log a debug message
     *
     * @param message Message to log
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    static void debug(const std::string& message,
                     const std::string& file = "", int line = 0);

    /**
     * @brief Log an info message
     *
     * @param message Message to log
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    static void info(const std::string& message,
                    const std::string& file = "", int line = 0);

    /**
     * @brief Log a warning message
     *
     * @param message Message to log
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    static void warning(const std::string& message,
                       const std::string& file = "", int line = 0);

    /**
     * @brief Log an error message
     *
     * @param message Message to log
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    static void error(const std::string& message,
                     const std::string& file = "", int line = 0);

    /**
     * @brief Log a fatal error message and terminate the application
     *
     * @param message Message to log
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    static void fatal(const std::string& message,
                     const std::string& file = "", int line = 0);

private:
    static std::string getCurrentTime();
    static std::string levelToString(Level level);
    static void writeToFile(const std::string& message);
    static void writeToConsole(const std::string& message, Level level);

    static std::ofstream logFile;
    static Level minLevel;
    static bool initialized;
    static bool fileLoggingEnabled;

    // Get the log mutex (function-local static to avoid initialization order issues)
    static std::mutex& getLogMutex();
};

// Convenience macros for logging with file and line information
#define LOG_DEBUG(msg) RYBlinnPhong::Logger::debug(msg, __FILE__, __LINE__)
#define LOG_INFO(msg) RYBlinnPhong::Logger::info(msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) RYBlinnPhong::Logger::warning(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) RYBlinnPhong::Logger::error(msg, __FILE__, __LINE__)
#define LOG_FATAL(msg) RYBlinnPhong::Logger::fatal(msg, __FILE__, __LINE__)

} // namespace RYBlinnPhong