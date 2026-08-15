if (this.importScripts) {
    importScripts('../../../resources/js-test.js');
    importScripts('shared.js');
}

description("This test checks that the origin private file system stores bytes exactly, including 0x00 and 0xFF, through both a writable stream and a sync access handle.");

var rootHandle, dirHandle, nestedHandle, fileHandle;
var resolvedPath, names, file, fileBytes, readBytes, readSize, fileSize;

const content = new Uint8Array([0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x00, 0x41]);

function bytesToString(buffer)
{
    return Array.from(new Uint8Array(buffer)).join(",");
}

async function directoryNames(handle)
{
    var result = [];
    for await (var name of handle.keys())
        result.push(name);
    return result.sort();
}

async function test()
{
    try {
        rootHandle = await navigator.storage.getDirectory();
        // Start from a known state: other tests share this origin's file system.
        await rootHandle.removeEntry("round-trip-dir", { "recursive" : true }).then(() => { }, () => { });

        debug("Create a nested directory and a file in it:");
        dirHandle = await rootHandle.getDirectoryHandle("round-trip-dir", { "create" : true });
        nestedHandle = await dirHandle.getDirectoryHandle("nested", { "create" : true });
        fileHandle = await nestedHandle.getFileHandle("binary.bin", { "create" : true });
        resolvedPath = (await rootHandle.resolve(fileHandle)).join("/");
        shouldBeEqualToString("resolvedPath", "round-trip-dir/nested/binary.bin");

        debug("Write binary content with a writable stream and read it back with getFile():");
        var writable = await fileHandle.createWritable();
        await writable.write(content);
        await writable.close();

        file = await fileHandle.getFile();
        shouldBe("file.size", "content.byteLength");
        fileBytes = bytesToString(await file.arrayBuffer());
        shouldBeEqualToString("fileBytes", bytesToString(content));

        debug("Read the same bytes back through a sync access handle:");
        var accessHandle = await fileHandle.createSyncAccessHandle();
        fileSize = accessHandle.getSize();
        shouldBe("fileSize", "content.byteLength");

        var buffer = new ArrayBuffer(content.byteLength);
        readSize = accessHandle.read(buffer, { "at" : 0 });
        shouldBe("readSize", "content.byteLength");
        readBytes = bytesToString(buffer);
        shouldBeEqualToString("readBytes", bytesToString(content));

        debug("Write past the end of the file: the gap reads back as zeros:");
        accessHandle.write(new Uint8Array([0xff]), { "at" : content.byteLength + 2 });
        accessHandle.flush();
        fileSize = accessHandle.getSize();
        shouldBe("fileSize", "content.byteLength + 3");

        buffer = new ArrayBuffer(3);
        accessHandle.read(buffer, { "at" : content.byteLength });
        readBytes = bytesToString(buffer);
        shouldBeEqualToString("readBytes", "0,0,255");

        debug("Truncate back to the original content:");
        accessHandle.truncate(content.byteLength);
        fileSize = accessHandle.getSize();
        shouldBe("fileSize", "content.byteLength");
        accessHandle.close();

        debug("The writable stream sees the truncated file too:");
        file = await fileHandle.getFile();
        fileBytes = bytesToString(await file.arrayBuffer());
        shouldBeEqualToString("fileBytes", bytesToString(content));

        debug("Remove the tree recursively:");
        await rootHandle.removeEntry("round-trip-dir", { "recursive" : true });
        names = await directoryNames(rootHandle);
        shouldBe("names.indexOf('round-trip-dir')", "-1");

        finishTest();
    } catch (error) {
        finishTest(error);
    }
}

test();
