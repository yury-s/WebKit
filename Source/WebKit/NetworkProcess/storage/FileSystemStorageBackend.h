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

#pragma once

#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/UniqueRef.h>
#include <wtf/text/WTFString.h>

namespace WebKit {

// Storage medium backing the Origin Private File System of one origin.
// FileSystemStorageManager and FileSystemStorageHandle route every file operation
// through this interface, so that a session which has no storage directory on disk
// can be served from memory instead, the way LocalStorage, IndexedDB and
// CacheStorage already are.
//
// Paths are opaque to callers: they are built with the FileSystem:: path string
// helpers from the root path this backend reports, and are only ever interpreted
// by the backend that produced them. The memory backend's paths are virtual and
// never reach the platform file system.
class FileSystemStorageBackend {
    WTF_MAKE_NONCOPYABLE(FileSystemStorageBackend);
public:
    // An empty root path means the session keeps no files on disk, and selects the
    // memory backend. This mirrors IDBStorageManager::createBackingStore().
    static UniqueRef<FileSystemStorageBackend> create(String&& rootPath);

    FileSystemStorageBackend() = default;
    virtual ~FileSystemStorageBackend() = default;

    virtual const String& rootPath() const LIFETIME_BOUND = 0;

    // True when the backend keeps file contents in memory only. Callers use this to
    // decide between handing the web process a file descriptor and serving it over IPC.
    virtual bool isMemoryBacked() const = 0;

    virtual bool fileExists(const String& path) = 0;
    virtual std::optional<FileSystem::FileType> fileType(const String& path) = 0;
    virtual std::optional<uint64_t> fileSize(const String& path) = 0;
    virtual Vector<String> listDirectory(const String& path) = 0;

    virtual bool makeAllDirectories(const String& path) = 0;
    // Creates an empty file, or leaves an existing one untouched. Returns whether the
    // file exists and is writable afterwards.
    virtual bool createFile(const String& path) = 0;
    virtual bool deleteFile(const String& path) = 0;
    virtual bool deleteEmptyDirectory(const String& path) = 0;
    virtual bool deleteNonEmptyDirectory(const String& path) = 0;
    virtual bool moveFile(const String& sourcePath, const String& destinationPath) = 0;
    virtual bool copyFile(const String& destinationPath, const String& sourcePath) = 0;

    // Scratch file staging the contents of a FileSystemWritableFileStream. It lives
    // outside the origin's tree, so it is invisible to listDirectory() and does not
    // count towards usage, matching the temporary file the disk backend has always used.
    virtual String createTemporaryFile() = 0;

    // An open file with a cursor, used for the writable stream scratch file.
    class OpenFile {
        WTF_MAKE_NONCOPYABLE(OpenFile);
    public:
        OpenFile() = default;
        virtual ~OpenFile() = default;

        virtual std::optional<uint64_t> seek(int64_t offset, FileSystem::FileSeekOrigin) = 0;
        virtual bool write(std::span<const uint8_t>) = 0;
        virtual bool truncate(uint64_t size) = 0;
    };
    virtual std::unique_ptr<OpenFile> openFile(const String& path) = 0;

    // Whole contents of a file, for serving getFile() when the web process cannot be
    // given a path to open.
    virtual std::optional<Vector<uint8_t>> readFile(const String& path) = 0;

    // Reads into and writes over an absolute range of a file, growing it with zeros
    // where the range starts past its end. Used to service sync access handles that
    // are backed by memory rather than a file descriptor.
    virtual std::optional<uint64_t> readFileRange(const String& path, uint64_t offset, std::span<uint8_t>) = 0;
    virtual std::optional<uint64_t> writeFileRange(const String& path, uint64_t offset, std::span<const uint8_t>) = 0;
    virtual bool truncateFile(const String& path, uint64_t size) = 0;

    // A file descriptor the web process can perform synchronous I/O on directly. Only
    // the disk backend has one; a memory-backed file is served over IPC instead.
    virtual FileSystem::FileHandle openFileForDirectAccess(const String& path) = 0;

    // Bytes held in memory by this backend, for quota accounting. Zero for the disk
    // backend, whose usage is measured from the file system by OriginStorageManager.
    virtual uint64_t memoryUsage() const = 0;

    // Whether the origin has any entry that exists only in memory, for reporting
    // website data. Always false for the disk backend, whose data is found by listing
    // its directory. Memory-backed data is discarded by dropping the backend, so there
    // is no separate clear step.
    virtual bool hasDataInMemory() const = 0;
};

} // namespace WebKit
