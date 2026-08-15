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

#include "FileSystemStorageBackend.h"

namespace WebKit {

// Serves the Origin Private File System from a directory on disk. Every operation
// forwards to the platform file system, which is what FileSystemStorageHandle did
// directly before the backend interface existed.
class FileSystemStorageDiskBackend final : public FileSystemStorageBackend {
    WTF_MAKE_TZONE_ALLOCATED(FileSystemStorageDiskBackend);
public:
    explicit FileSystemStorageDiskBackend(String&& rootPath);

private:
    const String& rootPath() const LIFETIME_BOUND final { return m_rootPath; }
    bool isMemoryBacked() const final { return false; }

    bool fileExists(const String&) final;
    std::optional<FileSystem::FileType> fileType(const String&) final;
    std::optional<uint64_t> fileSize(const String&) final;
    Vector<String> listDirectory(const String&) final;

    bool makeAllDirectories(const String&) final;
    bool createFile(const String&) final;
    bool deleteFile(const String&) final;
    bool deleteEmptyDirectory(const String&) final;
    bool deleteNonEmptyDirectory(const String&) final;
    bool moveFile(const String& sourcePath, const String& destinationPath) final;
    bool copyFile(const String& destinationPath, const String& sourcePath) final;

    String createTemporaryFile() final;
    std::unique_ptr<OpenFile> openFile(const String&) final;

    std::optional<Vector<uint8_t>> readFile(const String&) final;
    std::optional<uint64_t> readFileRange(const String&, uint64_t offset, std::span<uint8_t>) final;
    std::optional<uint64_t> writeFileRange(const String&, uint64_t offset, std::span<const uint8_t>) final;
    bool truncateFile(const String&, uint64_t size) final;

    FileSystem::FileHandle openFileForDirectAccess(const String&) final;

    uint64_t memoryUsage() const final { return 0; }
    bool hasDataInMemory() const final { return false; }

    String m_rootPath;
};

} // namespace WebKit
