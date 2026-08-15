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
// through this interface rather than calling FileSystem:: themselves, so that the
// medium can be swapped without touching the handle logic.
//
// Paths are opaque to callers: they are built with the FileSystem:: path string
// helpers from the root path this backend reports, and are only ever interpreted
// by the backend that produced them.
class FileSystemStorageBackend {
    WTF_MAKE_NONCOPYABLE(FileSystemStorageBackend);
public:
    FileSystemStorageBackend() = default;
    virtual ~FileSystemStorageBackend() = default;

    virtual const String& rootPath() const LIFETIME_BOUND = 0;

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
    // count towards usage.
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

    // A file descriptor the web process can perform synchronous I/O on directly.
    virtual FileSystem::FileHandle openFileForDirectAccess(const String& path) = 0;
};

} // namespace WebKit
