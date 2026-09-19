#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace mnc::darwin {

using fd_t = std::int32_t;
constexpr fd_t kStdin = 0;
constexpr fd_t kStdout = 1;
constexpr fd_t kStderr = 2;
constexpr fd_t kInvalidFd = -1;

enum class OpenMode { ReadOnly, WriteOnly, ReadWrite, CreateOrTruncate };

class FileDescriptorTable {
public:
    FileDescriptorTable();
    ~FileDescriptorTable();
    FileDescriptorTable(const FileDescriptorTable&) = delete;
    FileDescriptorTable& operator=(const FileDescriptorTable&) = delete;

    fd_t open_file(const std::filesystem::path& path, OpenMode mode);
    std::ptrdiff_t read(fd_t fd, void* buffer, std::size_t size);
    std::ptrdiff_t write(fd_t fd, const void* buffer, std::size_t size);
    fd_t duplicate(fd_t fd);
    bool close(fd_t fd);
    bool valid(fd_t fd) const;
    std::string last_error() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace mnc::darwin
