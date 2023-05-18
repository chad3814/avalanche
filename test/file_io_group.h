/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <unordered_set>

extern "C" {
#include <libavformat/avformat.h>
}

#include "../private/utils.h"

#include "../custom_io_group.h"
#include "../utils.h"

#include "./file_io.h"

class FileIoGroup : public Avalanche::CustomIoGroup {
public:

    virtual ~FileIoGroup() {
        for (auto file_io: file_ios) {
            delete file_io;
        }
        file_ios.clear();
    }

    AVIOContext * open(const std::string &uri) override {
        //log(LOG_INFO, "in FileIoGroup open %s\n", uri.c_str());
        auto file_io = new FileIo(uri);

        if (!file_io->open()) {
            delete file_io;
            m_is_aborting = true;
            return NULL;
        }
        file_ios.insert(file_io);

        return file_io->getAvioContext();
    }

    void close(void *opaque) override {
        FileIo *file_io = static_cast<FileIo *>(opaque);
        //log(LOG_INFO, "in FileIoGroup close %s\n", file_io->getUri().c_str());
        file_ios.erase(file_io);
        delete file_io;
    }

    int interruptCallback() override {
        if (m_is_aborting) {
            return 1;
        }
        return 0;
    }

private:
    std::unordered_set<FileIo *> file_ios;

    bool m_is_aborting = false;
};
