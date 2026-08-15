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

#pragma once

#include "ActiveDOMObject.h"
#include "BufferSource.h"
#include "FileSystemSyncAccessHandleIdentifier.h"
#include "IDLTypes.h"
#include <wtf/Deque.h>
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/Noncopyable.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/UniqueRef.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class FileSystemFileHandle;
template<typename> class DOMPromiseDeferred;
template<typename> class ExceptionOr;

class FileSystemSyncAccessHandle : public RefCounted<FileSystemSyncAccessHandle>, public ActiveDOMObject {
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    struct FilesystemReadWriteOptions {
        std::optional<unsigned long long> at;
    };

    static Ref<FileSystemSyncAccessHandle> create(ScriptExecutionContext&, FileSystemFileHandle&, FileSystemSyncAccessHandleIdentifier, FileSystem::FileHandle&&, uint64_t capacity);
    ~FileSystemSyncAccessHandle();

    ExceptionOr<void> truncate(unsigned long long size);
    ExceptionOr<unsigned long long> getSize();
    ExceptionOr<void> flush();
    ExceptionOr<void> close();
    ExceptionOr<unsigned long long> read(BufferSource&&, FilesystemReadWriteOptions);
    ExceptionOr<unsigned long long> write(BufferSource&&, FilesystemReadWriteOptions);
    void invalidate();

private:
    // The file this handle reads and writes. A file with a descriptor is served directly
    // from this process; one that exists only in the storage process's memory, as in an
    // ephemeral session, is served over IPC. "Sync" describes the JavaScript API, not the
    // transport: these handles are exposed on worker threads only, so blocking one on a
    // message is acceptable.
    class Delegate {
        WTF_MAKE_NONCOPYABLE(Delegate);
    public:
        Delegate() = default;
        virtual ~Delegate() = default;

        virtual std::optional<uint64_t> size() = 0;
        virtual std::optional<uint64_t> read(std::span<uint8_t>, uint64_t offset) = 0;
        virtual std::optional<uint64_t> write(std::span<const uint8_t>, uint64_t offset) = 0;
        virtual bool truncate(uint64_t size) = 0;
        virtual bool flush() = 0;
        virtual void close() = 0;
    };
    class FileHandleDelegate;
    class IPCDelegate;
    static UniqueRef<Delegate> createDelegate(FileSystemFileHandle&, FileSystemSyncAccessHandleIdentifier, FileSystem::FileHandle&&);

    FileSystemSyncAccessHandle(ScriptExecutionContext&, FileSystemFileHandle&, FileSystemSyncAccessHandleIdentifier, FileSystem::FileHandle&&, uint64_t capacity);
    using CloseCallback = CompletionHandler<void(ExceptionOr<void>&&)>;
    enum class ShouldNotifyBackend : bool { No, Yes };
    void closeInternal(ShouldNotifyBackend);
    bool requestSpaceForNewSize(uint64_t newSize);
    bool requestSpaceForWrite(uint64_t writeOffset, uint64_t writeLength);

    // ActiveDOMObject.
    void stop() final;

    const Ref<FileSystemFileHandle> m_source;
    FileSystemSyncAccessHandleIdentifier m_identifier;
    const UniqueRef<Delegate> m_file;
    // The file cursor, kept here rather than in the delegate so both transports share it.
    uint64_t m_offset { 0 };
    bool m_isClosed { false };
    uint64_t m_capacity;
};

} // namespace WebCore
