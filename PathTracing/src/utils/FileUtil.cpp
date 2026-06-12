#include "FileUtil.hpp"
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace RYRayTracing {

std::vector<char> FileUtil::readBinaryFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // Get file size
    size_t fileSize = static_cast<size_t>(file.tellg());

    // Create buffer
    std::vector<char> buffer(fileSize);

    // Read file
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

std::string FileUtil::readTextFile(const std::string& filename) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // Read entire file into string
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    return content;
}

bool FileUtil::fileExists(const std::string& filename) {
    // Use C++17 filesystem if available
#ifdef __cpp_lib_filesystem
    return std::filesystem::exists(filename);
#else
    // Fallback using platform-specific APIs
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(filename.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
#endif
#endif
}

size_t FileUtil::getFileSize(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    return static_cast<size_t>(file.tellg());
}

std::string FileUtil::getFileExtension(const std::string& filename) {
    size_t dotPos = filename.find_last_of('.');

    if (dotPos == std::string::npos) {
        return "";
    }

    return filename.substr(dotPos);
}

std::string FileUtil::getFileNameWithoutExtension(const std::string& filename) {
    size_t slashPos = filename.find_last_of("/\\");
    std::string nameOnly = (slashPos == std::string::npos) ?
                          filename : filename.substr(slashPos + 1);

    size_t dotPos = nameOnly.find_last_of('.');
    if (dotPos == std::string::npos) {
        return nameOnly;
    }

    return nameOnly.substr(0, dotPos);
}

std::string FileUtil::getDirectory(const std::string& filepath) {
    size_t slashPos = filepath.find_last_of("/\\");

    if (slashPos == std::string::npos) {
        return "";
    }

    return filepath.substr(0, slashPos);
}

std::string FileUtil::combinePath(const std::string& directory, const std::string& filename) {
    if (directory.empty()) {
        return filename;
    }

    // Check if directory ends with separator
    bool hasSeparator = !directory.empty() &&
                       (directory.back() == '/' || directory.back() == '\\');

    if (hasSeparator) {
        return directory + filename;
    } else {
#ifdef _WIN32
        return directory + "\\" + filename;
#else
        return directory + "/" + filename;
#endif
    }
}

} // namespace RYRayTracing