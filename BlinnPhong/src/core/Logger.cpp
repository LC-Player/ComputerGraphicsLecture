#include "Logger.hpp"
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace RYBlinnPhong {

// Static member initialization
std::ofstream Logger::logFile;
Logger::Level Logger::minLevel = Logger::Level::INFO;
bool Logger::initialized = false;
bool Logger::fileLoggingEnabled = false;

// Use function-local static for mutex to avoid static initialization order issues
std::mutex& Logger::getLogMutex() {
    static std::mutex logMutex;
    return logMutex;
}

void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(getLogMutex());

    if (initialized) {
        return;
    }

    if (!logFilePath.empty()) {
        logFile.open(logFilePath, std::ios::out | std::ios::app);
        if (logFile.is_open()) {
            fileLoggingEnabled = true;
            logFile << "\n=== Logging started at " << getCurrentTime() << " ===\n";
            logFile.flush();
        } else {
            std::cerr << "Warning: Could not open log file: " << logFilePath << std::endl;
        }
    }

    initialized = true;
    // Don't call info() here as it would cause recursive locking
    // Instead, write directly to console if needed
    std::cout << "[INFO] Logging system initialized" << std::endl;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(getLogMutex());

    if (!initialized) {
        return;
    }

    // Don't call info() here as it would cause recursive locking
    // Instead, write directly to console if needed
    std::cout << "[INFO] Logging system shutting down" << std::endl;

    if (fileLoggingEnabled && logFile.is_open()) {
        logFile << "=== Logging stopped at " << getCurrentTime() << " ===\n\n";
        logFile.close();
        fileLoggingEnabled = false;
    }

    initialized = false;
}

void Logger::setMinLevel(Level level) {
    std::lock_guard<std::mutex> lock(getLogMutex());
    minLevel = level;
}

void Logger::log(Level level, const std::string& message,
                const std::string& file, int line) {
    if (!initialized || level < minLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(getLogMutex());

    // Format the log message
    std::ostringstream oss;
    oss << "[" << getCurrentTime() << "] "
        << "[" << levelToString(level) << "] ";

    if (!file.empty()) {
        // Extract just the filename from the full path
        size_t pos = file.find_last_of("/\\");
        std::string filename = (pos != std::string::npos) ? file.substr(pos + 1) : file;
        oss << "[" << filename;
        if (line > 0) {
            oss << ":" << line;
        }
        oss << "] ";
    }

    oss << message;

    std::string formattedMessage = oss.str();

    // Write to console
    writeToConsole(formattedMessage, level);

    // Write to file if enabled
    if (fileLoggingEnabled) {
        writeToFile(formattedMessage);
    }

    // For fatal errors, throw an exception
    if (level == Level::FATAL) {
        throw std::runtime_error("FATAL ERROR: " + message);
    }
}

void Logger::debug(const std::string& message,
                  const std::string& file, int line) {
    log(Level::DEBUG, message, file, line);
}

void Logger::info(const std::string& message,
                 const std::string& file, int line) {
    log(Level::INFO, message, file, line);
}

void Logger::warning(const std::string& message,
                    const std::string& file, int line) {
    log(Level::WARNING, message, file, line);
}

void Logger::error(const std::string& message,
                  const std::string& file, int line) {
    log(Level::ERR, message, file, line);
}

void Logger::fatal(const std::string& message,
                  const std::string& file, int line) {
    log(Level::FATAL, message, file, line);
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;

    // Use localtime_s for Windows, localtime_r for others
#ifdef _WIN32
    std::tm tm;
    localtime_s(&tm, &time);
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
#else
    std::tm tm;
    localtime_r(&time, &tm);
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
#endif

    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::levelToString(Level level) {
    switch (level) {
        case Level::DEBUG:   return "DEBUG";
        case Level::INFO:    return "INFO";
        case Level::WARNING: return "WARNING";
        case Level::ERR:   return "ERROR";
        case Level::FATAL:   return "FATAL";
        default:             return "UNKNOWN";
    }
}

void Logger::writeToFile(const std::string& message) {
    if (logFile.is_open()) {
        logFile << message << std::endl;
        logFile.flush();
    }
}

void Logger::writeToConsole(const std::string& message, Level level) {
    // Set console color based on log level (Windows only)
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD originalColor = 0;

    if (hConsole != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
        GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
        originalColor = consoleInfo.wAttributes;

        switch (level) {
            case Level::DEBUG:
                SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                break;
            case Level::INFO:
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                break;
            case Level::WARNING:
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                break;
            case Level::ERR:
            case Level::FATAL:
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                break;
            default:
                break;
        }
    }
#endif

    std::cout << message << std::endl;

#ifdef _WIN32
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, originalColor);
    }
#endif
}

} // namespace RYBlinnPhong