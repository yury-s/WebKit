/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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
#include "FileSystemSyncAccessHandle.h"

#include "BufferSource.h"
#include "FileSystemFileHandle.h"
#include "JSDOMPromiseDeferred.h"
#include <wtf/CompletionHandler.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

// The file was opened in this process, so read and write it directly.
class FileSystemSyncAccessHandle::FileHandleDelegate final : public FileSystemSyncAccessHandle::Delegate {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(FileHandleDelegate);
public:
    explicit FileHandleDelegate(FileSystem::FileHandle&& file)
        : m_file(WTF::move(file))
    {
    }

private:
    std::optional<uint64_t> size() final { return m_file.size(); }

    std::optional<uint64_t> read(std::span<uint8_t> buffer, uint64_t offset) final
    {
        if (!m_file.seek(offset, FileSystem::FileSeekOrigin::Beginning))
            return std::nullopt;

        return m_file.read(buffer);
    }

    std::optional<uint64_t> write(std::span<const uint8_t> data, uint64_t offset) final
    {
        if (!m_file.seek(offset, FileSystem::FileSeekOrigin::Beginning))
            return std::nullopt;

        return m_file.write(data);
    }

    bool truncate(uint64_t size) final { return m_file.truncate(size); }
    bool flush() final { return m_file.flush(); }
    void close() final { m_file = { }; }

    FileSystem::FileHandle m_file;
};

Ref<FileSystemSyncAccessHandle> FileSystemSyncAccessHandle::create(ScriptExecutionContext& context, FileSystemFileHandle& source, FileSystemSyncAccessHandleIdentifier identifier, FileSystem::FileHandle&& file, uint64_t capacity)
{
    auto handle = adoptRef(*new FileSystemSyncAccessHandle(context, source, identifier, WTF::move(file), capacity));
    handle->suspendIfNeeded();
    return handle;
}

FileSystemSyncAccessHandle::FileSystemSyncAccessHandle(ScriptExecutionContext& context, FileSystemFileHandle& source, FileSystemSyncAccessHandleIdentifier identifier, FileSystem::FileHandle&& file, uint64_t capacity)
    : ActiveDOMObject(&context)
    , m_source(source)
    , m_identifier(identifier)
    , m_file(makeUniqueRef<FileHandleDelegate>(WTF::move(file)))
    , m_capacity(capacity)
{
    m_source->registerSyncAccessHandle(m_identifier, *this);
}

FileSystemSyncAccessHandle::~FileSystemSyncAccessHandle()
{
    m_source->unregisterSyncAccessHandle(m_identifier);
    closeInternal(ShouldNotifyBackend::Yes);
}

// https://fs.spec.whatwg.org/#dom-filesystemsyncaccesshandle-truncate
ExceptionOr<void> FileSystemSyncAccessHandle::truncate(unsigned long long size)
{
    if (m_isClosed)
        return Exception { ExceptionCode::InvalidStateError, "AccessHandle is closed"_s };

    auto oldSize = m_file->size();
    if (!oldSize)
        return Exception { ExceptionCode::InvalidStateError, "Failed to get current size"_s };

    if (size > *oldSize && !requestSpaceForNewSize(size))
        return Exception { ExceptionCode::QuotaExceededError };

    if (!m_file->truncate(size))
        return Exception { ExceptionCode::InvalidStateError, "Failed to truncate file"_s };

    if (m_offset > size)
        m_offset = size;

    return { };
}

ExceptionOr<unsigned long long> FileSystemSyncAccessHandle::getSize()
{
    if (m_isClosed)
        return Exception { ExceptionCode::InvalidStateError, "AccessHandle is closed"_s };

    auto result = m_file->size();
    return result ? ExceptionOr<unsigned long long> { result.value() } : Exception { ExceptionCode::InvalidStateError, "Failed to get file size"_s };
}

ExceptionOr<void> FileSystemSyncAccessHandle::flush()
{
    if (m_isClosed)
        return Exception { ExceptionCode::InvalidStateError, "AccessHandle is closed"_s };

    bool succeeded = m_file->flush();
    return succeeded ? ExceptionOr<void> { } : Exception { ExceptionCode::InvalidStateError, "Failed to flush file"_s };
}

ExceptionOr<void> FileSystemSyncAccessHandle::close()
{
    if (m_isClosed)
        return { };

    closeInternal(ShouldNotifyBackend::Yes);
    return { };
}

void FileSystemSyncAccessHandle::closeInternal(ShouldNotifyBackend shouldNotifyBackend)
{
    if (m_isClosed)
        return;

    m_isClosed = true;
    m_file->close();

    if (shouldNotifyBackend == ShouldNotifyBackend::Yes)
        m_source->closeSyncAccessHandle(m_identifier);
}

ExceptionOr<unsigned long long> FileSystemSyncAccessHandle::read(BufferSource&& buffer, FileSystemSyncAccessHandle::FilesystemReadWriteOptions options)
{
    if (m_isClosed)
        return Exception { ExceptionCode::InvalidStateError, "AccessHandle is closed"_s };

    auto readOffset = options.at.value_or(m_offset);
    auto result = m_file->read(buffer.mutableSpan(), readOffset);
    if (!result)
        return Exception { ExceptionCode::InvalidStateError, "Failed to read from file"_s };

    m_offset = readOffset + *result;
    return *result;
}

ExceptionOr<unsigned long long> FileSystemSyncAccessHandle::write(BufferSource&& buffer, FileSystemSyncAccessHandle::FilesystemReadWriteOptions options)
{
    if (m_isClosed)
        return Exception { ExceptionCode::InvalidStateError, "AccessHandle is closed"_s };

    auto writeOffset = options.at.value_or(m_offset);
    if (!requestSpaceForWrite(writeOffset, buffer.byteLength()))
        return Exception { ExceptionCode::QuotaExceededError };

    auto result = m_file->write(buffer.span(), writeOffset);
    if (!result)
        return Exception { ExceptionCode::InvalidStateError, "Failed to write to file"_s };

    m_offset = writeOffset + *result;
    return *result;
}

void FileSystemSyncAccessHandle::stop()
{
    closeInternal(ShouldNotifyBackend::Yes);
}

void FileSystemSyncAccessHandle::invalidate()
{
    // Invalidation is initiated by backend.
    closeInternal(ShouldNotifyBackend::No);
}

bool FileSystemSyncAccessHandle::requestSpaceForNewSize(uint64_t newSize)
{
    if (newSize <= m_capacity)
        return true;

    auto newCapacity = m_source->requestNewCapacityForSyncAccessHandle(m_identifier, (uint64_t)newSize);
    if (newCapacity)
        m_capacity = *newCapacity;

    return newSize <= m_capacity;
}

bool FileSystemSyncAccessHandle::requestSpaceForWrite(uint64_t writeOffset, uint64_t writeLength)
{
    CheckedUint64 newSize = writeOffset;
    newSize += writeLength;
    if (newSize.hasOverflowed())
        return false;

    return requestSpaceForNewSize(newSize);
}

} // namespace WebCore
