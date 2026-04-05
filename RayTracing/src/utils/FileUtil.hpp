#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

namespace RYRayTracing {

/**
 * @brief File utility functions for reading binary and text files
 */
class FileUtil {
public:
    /**
     * @brief Read a binary file into a vector of bytes
     *
     * @param filename Path to the file
     * @return std::vector<char> File contents as bytes
     * @throws std::runtime_error if file cannot be opened
     */
    static std::vector<char> readBinaryFile(const std::string& filename);

    /**
     * @brief Read a text file into a string
     *
     * @param filename Path to the file
     * @return std::string File contents as text
     * @throws std::runtime_error if file cannot be opened
     */
    static std::string readTextFile(const std::string& filename);

    /**
     * @brief Check if a file exists
     *
     * @param filename Path to the file
     * @return true if file exists, false otherwise
     */
    static bool fileExists(const std::string& filename);

    /**
     * @brief Get the file size in bytes
     *
     * @param filename Path to the file
     * @return size_t File size in bytes
     * @throws std::runtime_error if file cannot be opened
     */
    static size_t getFileSize(const std::string& filename);

    /**
     * @brief Get the file extension
     *
     * @param filename Path to the file
     * @return std::string File extension (including the dot)
     */
    static std::string getFileExtension(const std::string& filename);

    /**
     * @brief Get the file name without extension
     *
     * @param filename Path to the file
     * @return std::string File name without extension
     */
    static std::string getFileNameWithoutExtension(const std::string& filename);

    /**
     * @brief Get the directory part of a file path
     *
     * @param filepath Full file path
     * @return std::string Directory path
     */
    static std::string getDirectory(const std::string& filepath);

    /**
     * @brief Combine directory and file name to create a full path
     *
     * @param directory Directory path
     * @param filename File name
     * @return std::string Combined full path
     */
    static std::string combinePath(const std::string& directory, const std::string& filename);

private:
    // Private constructor to prevent instantiation
    FileUtil() = delete;
    ~FileUtil() = delete;
};

} // namespace RYRayTracing