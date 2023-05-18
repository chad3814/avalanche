/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <unordered_set>

extern "C" {
#include <libavformat/avformat.h>
}

#include <uv.h>

#include "../private/utils.h"

#include "../utils.h"

class ResourceIoGroup;

class ResourceIo {
public:
    ResourceIo(ResourceIoGroup *resource_io_group, const std::string &uri, int64_t file_size);
    ~ResourceIo();

    AVIOContext * getAvioContext() {
        return m_avio_context;
    }

    const std::string & getUri() {
        return m_uri;
    }

    static int libavRead(void *this_ptr, uint8_t *buf, int buf_size) {
        return static_cast<ResourceIo *>(this_ptr)->read(buf, buf_size);
    }

    static int64_t libavSeek(void *this_ptr, int64_t offset, int whence) {
        return static_cast<ResourceIo *>(this_ptr)->seek(offset, whence);
    }

private:
    ResourceIoGroup *m_resource_io_group;
    std::string m_uri;
    int64_t m_file_size;

    AVIOContext *m_avio_context;

    int read(uint8_t *buf, int buf_size);
    int64_t seek(int64_t offset, int whence);
};
