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
#include "FileSystemStorageMemoryBackend.h"

#include <wtf/CheckedArithmetic.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringView.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(FileSystemStorageMemoryBackend);

// Virtual roots. They only ever exist as map keys: no operation on a memory-backed
// origin reaches the platform file system, so these paths are never created on disk.
// The two are siblings rather than nested, so that the scratch files staging writable
// streams are neither listed inside the origin's tree nor counted against its quota,
// matching the system temporary file the disk backend uses for the same purpose.
static constexpr auto memoryOriginRootPath = "/opfs-memory/origin"_s;
static constexpr auto memoryTemporaryDirectoryPath = "/opfs-memory/writables"_s;

static void resizeAndZeroFill(Vector<uint8_t>& data, size_t size)
{
    auto oldSize = data.size();
    data.resize(size);
    if (size > oldSize)
        zeroSpan(data.mutableSpan().subspan(oldSize));
}

namespace {

class MemoryOpenFile final : public FileSystemStorageBackend::OpenFile {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(MemoryOpenFile);
public:
    MemoryOpenFile(FileSystemStorageMemoryBackend& backend, const String& path)
        : m_backend(backend)
        , m_path(path)
    {
    }

private:
    std::optional<uint64_t> seek(int64_t offset, FileSystem::FileSeekOrigin origin) final
    {
        auto* backend = m_backend.get();
        if (!backend)
            return std::nullopt;

        CheckedInt64 newOffset { 0 };
        switch (origin) {
        case FileSystem::FileSeekOrigin::Beginning:
            newOffset = offset;
            break;
        case FileSystem::FileSeekOrigin::Current:
            newOffset = CheckedInt64 { static_cast<int64_t>(m_offset) } + offset;
            break;
        case FileSystem::FileSeekOrigin::End: {
            auto size = backend->fileSize(m_path);
            if (!size)
                return std::nullopt;
            newOffset = CheckedInt64 { static_cast<int64_t>(*size) } + offset;
            break;
        }
        }

        if (newOffset.hasOverflowed() || newOffset.value() < 0)
            return std::nullopt;

        m_offset = newOffset.value();
        return m_offset;
    }

    bool write(std::span<const uint8_t> data) final
    {
        auto* backend = m_backend.get();
        if (!backend)
            return false;

        auto written = backend->writeFileRange(m_path, m_offset, data);
        if (!written)
            return false;

        m_offset += *written;
        return true;
    }

    bool truncate(uint64_t size) final
    {
        auto* backend = m_backend.get();
        return backend && backend->truncateFile(m_path, size);
    }

    WeakPtr<FileSystemStorageMemoryBackend> m_backend;
    String m_path;
    uint64_t m_offset { 0 };
};

} // namespace

FileSystemStorageMemoryBackend::FileSystemStorageMemoryBackend()
    : m_rootPath(memoryOriginRootPath)
    , m_temporaryDirectoryPath(memoryTemporaryDirectoryPath)
{
    makeAllDirectories(m_rootPath);
    makeAllDirectories(m_temporaryDirectoryPath);
}

FileSystemStorageMemoryBackend::~FileSystemStorageMemoryBackend() = default;

auto FileSystemStorageMemoryBackend::entry(const String& path) -> Entry*
{
    auto iterator = m_entries.find(path);
    return iterator == m_entries.end() ? nullptr : &iterator->value;
}

auto FileSystemStorageMemoryBackend::entry(const String& path) const -> const Entry*
{
    auto iterator = m_entries.find(path);
    return iterator == m_entries.end() ? nullptr : &iterator->value;
}

auto FileSystemStorageMemoryBackend::fileEntry(const String& path) -> Entry*
{
    auto* result = entry(path);
    return result && result->type == FileSystem::FileType::Regular ? result : nullptr;
}

bool FileSystemStorageMemoryBackend::hasChildren(const String& path) const
{
    auto prefix = makeString(path, FileSystem::pathSeparator);
    for (auto& key : m_entries.keys()) {
        if (key.startsWith(prefix))
            return true;
    }
    return false;
}

Vector<String> FileSystemStorageMemoryBackend::pathAndDescendants(const String& path) const
{
    auto prefix = makeString(path, FileSystem::pathSeparator);
    Vector<String> result;
    for (auto& key : m_entries.keys()) {
        if (key == path || key.startsWith(prefix))
            result.append(key);
    }
    return result;
}

void FileSystemStorageMemoryBackend::removeSubtree(const String& path)
{
    for (auto& descendant : pathAndDescendants(path))
        m_entries.remove(descendant);
}

bool FileSystemStorageMemoryBackend::fileExists(const String& path)
{
    return m_entries.contains(path);
}

std::optional<FileSystem::FileType> FileSystemStorageMemoryBackend::fileType(const String& path)
{
    if (auto* result = entry(path))
        return result->type;

    return std::nullopt;
}

std::optional<uint64_t> FileSystemStorageMemoryBackend::fileSize(const String& path)
{
    auto* result = entry(path);
    if (!result)
        return std::nullopt;

    return result->type == FileSystem::FileType::Regular ? result->data.size() : 0;
}

Vector<String> FileSystemStorageMemoryBackend::listDirectory(const String& path)
{
    auto* directory = entry(path);
    if (!directory || directory->type != FileSystem::FileType::Directory)
        return { };

    auto prefix = makeString(path, FileSystem::pathSeparator);
    Vector<String> names;
    for (auto& key : m_entries.keys()) {
        if (!key.startsWith(prefix))
            continue;

        auto name = key.substring(prefix.length());
        if (name.isEmpty() || name.contains(FileSystem::pathSeparator))
            continue;

        names.append(WTF::move(name));
    }

    // The map iterates in hash order, which would make listings depend on the names
    // involved. Sorting keeps them stable.
    std::ranges::sort(names, codePointCompareLessThan);
    return names;
}

bool FileSystemStorageMemoryBackend::makeAllDirectories(const String& path)
{
    if (path.isEmpty())
        return false;

    // Virtual paths are absolute, so walking their components from the start creates
    // every missing ancestor before the directory itself.
    unsigned position = 0;
    while (true) {
        auto separator = path.find(FileSystem::pathSeparator, position);
        auto prefix = separator == notFound ? path : path.left(static_cast<unsigned>(separator));
        if (!prefix.isEmpty()) {
            auto& directory = m_entries.ensure(prefix, [] {
                return Entry { FileSystem::FileType::Directory, { } };
            }).iterator->value;
            if (directory.type != FileSystem::FileType::Directory)
                return false;
        }

        if (separator == notFound)
            return true;

        position = static_cast<unsigned>(separator) + 1;
    }
}

bool FileSystemStorageMemoryBackend::createFile(const String& path)
{
    if (auto* existing = entry(path))
        return existing->type == FileSystem::FileType::Regular;

    auto* parent = entry(FileSystem::parentPath(path));
    if (!parent || parent->type != FileSystem::FileType::Directory)
        return false;

    m_entries.add(path, Entry { FileSystem::FileType::Regular, { } });
    return true;
}

bool FileSystemStorageMemoryBackend::deleteFile(const String& path)
{
    if (!fileEntry(path))
        return false;

    m_entries.remove(path);
    return true;
}

bool FileSystemStorageMemoryBackend::deleteEmptyDirectory(const String& path)
{
    auto* directory = entry(path);
    if (!directory || directory->type != FileSystem::FileType::Directory || hasChildren(path))
        return false;

    m_entries.remove(path);
    return true;
}

bool FileSystemStorageMemoryBackend::deleteNonEmptyDirectory(const String& path)
{
    auto* directory = entry(path);
    if (!directory || directory->type != FileSystem::FileType::Directory)
        return false;

    removeSubtree(path);
    return true;
}

bool FileSystemStorageMemoryBackend::moveFile(const String& sourcePath, const String& destinationPath)
{
    if (sourcePath == destinationPath)
        return m_entries.contains(sourcePath);

    if (!m_entries.contains(sourcePath))
        return false;

    // A directory cannot be moved inside itself.
    if (destinationPath.startsWith(makeString(sourcePath, FileSystem::pathSeparator)))
        return false;

    auto* destinationParent = entry(FileSystem::parentPath(destinationPath));
    if (!destinationParent || destinationParent->type != FileSystem::FileType::Directory)
        return false;

    // Moving onto an existing entry replaces it, the way rename() does on disk.
    removeSubtree(destinationPath);
    for (auto& path : pathAndDescendants(sourcePath)) {
        auto moved = m_entries.take(path);
        m_entries.set(makeString(destinationPath, StringView { path }.substring(sourcePath.length())), WTF::move(moved));
    }
    return true;
}

bool FileSystemStorageMemoryBackend::copyFile(const String& destinationPath, const String& sourcePath)
{
    auto* source = fileEntry(sourcePath);
    if (!source)
        return false;

    if (auto* destination = entry(destinationPath); destination && destination->type != FileSystem::FileType::Regular)
        return false;

    auto* destinationParent = entry(FileSystem::parentPath(destinationPath));
    if (!destinationParent || destinationParent->type != FileSystem::FileType::Directory)
        return false;

    auto data = source->data;
    m_entries.set(destinationPath, Entry { FileSystem::FileType::Regular, WTF::move(data) });
    return true;
}

String FileSystemStorageMemoryBackend::createTemporaryFile()
{
    auto path = FileSystem::pathByAppendingComponent(m_temporaryDirectoryPath, String::number(m_nextTemporaryFileNumber++));
    m_entries.set(path, Entry { FileSystem::FileType::Regular, { } });
    return path;
}

std::unique_ptr<FileSystemStorageBackend::OpenFile> FileSystemStorageMemoryBackend::openFile(const String& path)
{
    if (!fileEntry(path))
        return nullptr;

    return makeUnique<MemoryOpenFile>(*this, path);
}

std::optional<Vector<uint8_t>> FileSystemStorageMemoryBackend::readFile(const String& path)
{
    auto* file = fileEntry(path);
    if (!file)
        return std::nullopt;

    return file->data;
}

std::optional<uint64_t> FileSystemStorageMemoryBackend::readFileRange(const String& path, uint64_t offset, std::span<uint8_t> buffer)
{
    auto* file = fileEntry(path);
    if (!file)
        return std::nullopt;

    if (offset >= file->data.size())
        return 0;

    size_t count = std::min<uint64_t>(buffer.size(), file->data.size() - offset);
    memcpySpan(buffer.first(count), file->data.subspan(offset, count));
    return count;
}

std::optional<uint64_t> FileSystemStorageMemoryBackend::writeFileRange(const String& path, uint64_t offset, std::span<const uint8_t> data)
{
    auto* file = fileEntry(path);
    if (!file)
        return std::nullopt;

    CheckedSize checkedEnd = CheckedSize { offset } + data.size();
    if (checkedEnd.hasOverflowed())
        return std::nullopt;

    // Writing past the end grows the file, and the gap reads back as zeros.
    auto end = checkedEnd.value();
    if (end > file->data.size())
        resizeAndZeroFill(file->data, end);

    if (!data.empty())
        memcpySpan(file->data.mutableSubspan(offset, data.size()), data);

    return data.size();
}

bool FileSystemStorageMemoryBackend::truncateFile(const String& path, uint64_t size)
{
    auto* file = fileEntry(path);
    if (!file)
        return false;

    if (size > std::numeric_limits<size_t>::max())
        return false;

    resizeAndZeroFill(file->data, size);
    return true;
}

uint64_t FileSystemStorageMemoryBackend::memoryUsage() const
{
    auto prefix = makeString(m_rootPath, FileSystem::pathSeparator);
    CheckedUint64 result = 0;
    for (auto& [path, storedEntry] : m_entries) {
        if (path.startsWith(prefix))
            result += storedEntry.data.size();
    }

    return result.hasOverflowed() ? 0 : result.value();
}

bool FileSystemStorageMemoryBackend::hasDataInMemory() const
{
    return hasChildren(m_rootPath);
}

} // namespace WebKit
