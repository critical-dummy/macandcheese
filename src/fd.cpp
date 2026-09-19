#include "mnc/fd.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mnc::darwin {
struct FileDescriptorTable::Impl {
    struct Entry {
#ifdef _WIN32
        void* handle = nullptr;
#else
        int handle = -1;
#endif
        bool used = false;
    };
    mutable std::mutex mutex;
    std::vector<Entry> entries;
    std::string error;
    Impl() : entries(3) {}
};

FileDescriptorTable::FileDescriptorTable() : impl_(new Impl) {}
FileDescriptorTable::~FileDescriptorTable() {
    for (fd_t fd = 3; fd < static_cast<fd_t>(impl_->entries.size()); ++fd) close(fd);
    delete impl_;
}

fd_t FileDescriptorTable::open_file(const std::filesystem::path& path, OpenMode mode) {
    std::lock_guard lock(impl_->mutex);
#ifdef _WIN32
    DWORD access = GENERIC_READ;
    DWORD creation = OPEN_EXISTING;
    if (mode == OpenMode::WriteOnly) access = GENERIC_WRITE;
    if (mode == OpenMode::ReadWrite) access = GENERIC_READ | GENERIC_WRITE;
    if (mode == OpenMode::CreateOrTruncate) { access = GENERIC_READ | GENERIC_WRITE; creation = CREATE_ALWAYS; }
    HANDLE h = CreateFileW(path.wstring().c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { impl_->error = "CreateFileW failed: " + std::to_string(GetLastError()); return kInvalidFd; }
    impl_->entries.push_back({h, true});
#else
    int flags = mode == OpenMode::ReadOnly ? O_RDONLY : mode == OpenMode::WriteOnly ? O_WRONLY : O_RDWR;
    if (mode == OpenMode::CreateOrTruncate) flags |= O_CREAT | O_TRUNC;
    int h = ::open(path.c_str(), flags, 0600);
    if (h < 0) { impl_->error = std::strerror(errno); return kInvalidFd; }
    impl_->entries.push_back({h, true});
#endif
    return static_cast<fd_t>(impl_->entries.size() - 1);
}

std::ptrdiff_t FileDescriptorTable::read(fd_t fd, void* buffer, std::size_t size) {
    std::lock_guard lock(impl_->mutex);
    if (!valid(fd)) return -1;
#ifdef _WIN32
    DWORD count = 0; if (!ReadFile(static_cast<HANDLE>(impl_->entries[fd].handle), buffer, static_cast<DWORD>(size), &count, nullptr)) { impl_->error = "ReadFile failed: " + std::to_string(GetLastError()); return -1; } return count;
#else
    return ::read(impl_->entries[fd].handle, buffer, size);
#endif
}

std::ptrdiff_t FileDescriptorTable::write(fd_t fd, const void* buffer, std::size_t size) {
    std::lock_guard lock(impl_->mutex);
    if (!valid(fd)) return -1;
#ifdef _WIN32
    DWORD count = 0; if (!WriteFile(static_cast<HANDLE>(impl_->entries[fd].handle), buffer, static_cast<DWORD>(size), &count, nullptr)) { impl_->error = "WriteFile failed: " + std::to_string(GetLastError()); return -1; } return count;
#else
    return ::write(impl_->entries[fd].handle, buffer, size);
#endif
}

fd_t FileDescriptorTable::duplicate(fd_t fd) {
    std::lock_guard lock(impl_->mutex);
    if (!valid(fd)) return kInvalidFd;
#ifdef _WIN32
    HANDLE copy = nullptr; if (!DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(impl_->entries[fd].handle), GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) { impl_->error = "DuplicateHandle failed: " + std::to_string(GetLastError()); return kInvalidFd; } impl_->entries.push_back({copy, true});
#else
    int copy = ::dup(impl_->entries[fd].handle); if (copy < 0) { impl_->error = std::strerror(errno); return kInvalidFd; } impl_->entries.push_back({copy, true});
#endif
    return static_cast<fd_t>(impl_->entries.size() - 1);
}

bool FileDescriptorTable::close(fd_t fd) {
    std::lock_guard lock(impl_->mutex);
    if (fd < 3 || fd >= static_cast<fd_t>(impl_->entries.size()) || !impl_->entries[fd].used) return false;
#ifdef _WIN32
    CloseHandle(static_cast<HANDLE>(impl_->entries[fd].handle));
#else
    ::close(impl_->entries[fd].handle);
#endif
    impl_->entries[fd].used = false;
    return true;
}

bool FileDescriptorTable::valid(fd_t fd) const {
    return fd >= 0 && fd < static_cast<fd_t>(impl_->entries.size()) && impl_->entries[fd].used;
}
std::string FileDescriptorTable::last_error() const { std::lock_guard lock(impl_->mutex); return impl_->error; }
} // namespace mnc::darwin
