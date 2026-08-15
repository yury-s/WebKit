/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FileSystemStorageDiskBackend.h"

#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(FileSystemStorageDiskBackend);

namespace {

class DiskOpenFile final : public FileSystemStorageBackend::OpenFile {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(DiskOpenFile);
public:
    explicit DiskOpenFile(FileSystem::FileHandle&& handle)
        : m_handle(WTF::move(handle))
    {
    }

private:
    std::optional<uint64_t> seek(int64_t offset, FileSystem::FileSeekOrigin origin) final
    {
        return m_handle.seek(offset, origin);
    }

    bool write(std::span<const uint8_t> data) final
    {
        return !!m_handle.write(data);
    }

    bool truncate(uint64_t size) final
    {
        return m_handle.truncate(size);
    }

    FileSystem::FileHandle m_handle;
};

} // namespace

FileSystemStorageDiskBackend::FileSystemStorageDiskBackend(String&& rootPath)
    : m_rootPath(WTF::move(rootPath))
{
    ASSERT(!m_rootPath.isEmpty());
}

bool FileSystemStorageDiskBackend::fileExists(const String& path)
{
    return FileSystem::fileExists(path);
}

std::optional<FileSystem::FileType> FileSystemStorageDiskBackend::fileType(const String& path)
{
    return FileSystem::fileType(path);
}

std::optional<uint64_t> FileSystemStorageDiskBackend::fileSize(const String& path)
{
    return FileSystem::fileSize(path);
}

Vector<String> FileSystemStorageDiskBackend::listDirectory(const String& path)
{
    return FileSystem::listDirectory(path);
}

bool FileSystemStorageDiskBackend::makeAllDirectories(const String& path)
{
    return FileSystem::makeAllDirectories(path);
}

bool FileSystemStorageDiskBackend::createFile(const String& path)
{
    return !!FileSystem::openFile(path, FileSystem::FileOpenMode::ReadWrite);
}

bool FileSystemStorageDiskBackend::deleteFile(const String& path)
{
    return FileSystem::deleteFile(path);
}

bool FileSystemStorageDiskBackend::deleteEmptyDirectory(const String& path)
{
    return FileSystem::deleteEmptyDirectory(path);
}

bool FileSystemStorageDiskBackend::deleteNonEmptyDirectory(const String& path)
{
    return FileSystem::deleteNonEmptyDirectory(path);
}

bool FileSystemStorageDiskBackend::moveFile(const String& sourcePath, const String& destinationPath)
{
    return FileSystem::moveFile(sourcePath, destinationPath);
}

bool FileSystemStorageDiskBackend::copyFile(const String& destinationPath, const String& sourcePath)
{
    return FileSystem::copyFile(destinationPath, sourcePath);
}

String FileSystemStorageDiskBackend::createTemporaryFile()
{
    return FileSystem::createTemporaryFile("FileSystemWritableStream"_s);
}

std::unique_ptr<FileSystemStorageBackend::OpenFile> FileSystemStorageDiskBackend::openFile(const String& path)
{
    auto handle = FileSystem::openFile(path, FileSystem::FileOpenMode::ReadWrite);
    if (!handle)
        return nullptr;

    return makeUnique<DiskOpenFile>(WTF::move(handle));
}

std::optional<Vector<uint8_t>> FileSystemStorageDiskBackend::readFile(const String& path)
{
    auto handle = FileSystem::openFile(path, FileSystem::FileOpenMode::Read);
    if (!handle)
        return std::nullopt;

    return handle.readAll();
}

std::optional<uint64_t> FileSystemStorageDiskBackend::readFileRange(const String& path, uint64_t offset, std::span<uint8_t> buffer)
{
    auto handle = FileSystem::openFile(path, FileSystem::FileOpenMode::Read);
    if (!handle)
        return std::nullopt;

    if (!handle.seek(offset, FileSystem::FileSeekOrigin::Beginning))
        return std::nullopt;

    return handle.read(buffer);
}

std::optional<uint64_t> FileSystemStorageDiskBackend::writeFileRange(const String& path, uint64_t offset, std::span<const uint8_t> data)
{
    auto handle = FileSystem::openFile(path, FileSystem::FileOpenMode::ReadWrite);
    if (!handle)
        return std::nullopt;

    if (!handle.seek(offset, FileSystem::FileSeekOrigin::Beginning))
        return std::nullopt;

    return handle.write(data);
}

bool FileSystemStorageDiskBackend::truncateFile(const String& path, uint64_t size)
{
    auto handle = FileSystem::openFile(path, FileSystem::FileOpenMode::ReadWrite);
    if (!handle)
        return false;

    return handle.truncate(size);
}

FileSystem::FileHandle FileSystemStorageDiskBackend::openFileForDirectAccess(const String& path)
{
    return FileSystem::openFile(path, FileSystem::FileOpenMode::ReadWrite);
}

} // namespace WebKit
