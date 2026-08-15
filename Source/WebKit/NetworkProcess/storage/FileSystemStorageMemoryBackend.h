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
#include <wtf/CheckedPtr.h>
#include <wtf/HashMap.h>
#include <wtf/WeakPtr.h>

namespace WebKit {

// Serves the Origin Private File System entirely from memory, for a session that has
// no storage directory on disk. It dies with the FileSystemStorageManager that owns
// it, so nothing outlives the session and nothing is ever written out.
//
// Entries are held in one flat map keyed by absolute virtual path; a directory's
// children are the entries one separator deeper. OPFS trees of an ephemeral session
// are small, so scanning the map beats maintaining a real tree.
class FileSystemStorageMemoryBackend final : public FileSystemStorageBackend, public CanMakeWeakPtr<FileSystemStorageMemoryBackend>, public CanMakeCheckedPtr<FileSystemStorageMemoryBackend> {
    WTF_MAKE_TZONE_ALLOCATED(FileSystemStorageMemoryBackend);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(FileSystemStorageMemoryBackend);
public:
    FileSystemStorageMemoryBackend();
    ~FileSystemStorageMemoryBackend();

    // Public so the OpenFile implementation, which holds only a path, can reach them.
    std::optional<uint64_t> readFileRange(const String&, uint64_t offset, std::span<uint8_t>) final;
    std::optional<uint64_t> writeFileRange(const String&, uint64_t offset, std::span<const uint8_t>) final;
    bool truncateFile(const String&, uint64_t size) final;
    std::optional<uint64_t> fileSize(const String&) final;

private:
    struct Entry {
        FileSystem::FileType type { FileSystem::FileType::Regular };
        Vector<uint8_t> data;
    };

    const String& rootPath() const LIFETIME_BOUND final { return m_rootPath; }
    bool isMemoryBacked() const final { return true; }

    bool fileExists(const String&) final;
    std::optional<FileSystem::FileType> fileType(const String&) final;
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

    FileSystem::FileHandle openFileForDirectAccess(const String&) final { return { }; }

    uint64_t memoryUsage() const final;
    bool hasDataInMemory() const final;

    Entry* entry(const String& path) LIFETIME_BOUND;
    const Entry* entry(const String& path) const LIFETIME_BOUND;
    // Entry that must be a regular file for the operation to make sense.
    Entry* fileEntry(const String& path) LIFETIME_BOUND;
    bool hasChildren(const String& path) const;
    Vector<String> pathAndDescendants(const String& path) const;
    void removeSubtree(const String& path);

    String m_rootPath;
    String m_temporaryDirectoryPath;
    uint64_t m_nextTemporaryFileNumber { 0 };
    HashMap<String, Entry> m_entries;
};

} // namespace WebKit
