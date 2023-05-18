/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <unordered_set>

extern "C" {
#include <libavformat/avformat.h>
}

#include "../private/utils.h"

#include "../utils.h"

class FileIo {
public:
    FileIo(const std::string &uri) :
        m_uri(uri) {
        int m_len_buffer = 128 * 1024;
        uint8_t * m_buffer = (unsigned char *)av_malloc(m_len_buffer);
        m_avio_context = avio_alloc_context(m_buffer, m_len_buffer, 0, this, &FileIo::libavRead, NULL, &FileIo::libavSeek);
    }

    ~FileIo() {
        if (m_fh) {
            fclose(m_fh);
            m_fh = NULL;
        }

        // we are responsible for this buffer too, though libav internals are allowed
        // to av_realloc it or even free it
        av_freep(&m_avio_context->buffer);
        av_freep(&m_avio_context);
    }

    AVIOContext * getAvioContext() {
        return m_avio_context;
    }

    const std::string & getUri() {
        return m_uri;
    }

    bool open() {
        m_fh = fopen(m_uri.c_str(), "rb");
        if (!m_fh) {
            //log(LOG_INFO, "failed to open %s\n", m_uri.c_str());
            return false;
        }
        return true;
    }

    static int libavRead(void *this_ptr, uint8_t *buf, int buf_size) {
        return static_cast<FileIo *>(this_ptr)->read(buf, buf_size);
    }

    static int64_t libavSeek(void *this_ptr, int64_t offset, int whence) {
        return static_cast<FileIo *>(this_ptr)->seek(offset, whence);
    }

private:
    std::string m_uri;
    AVIOContext *m_avio_context;
    FILE *m_fh;

    int read(uint8_t *buf, int buf_size) {
        //log(LOG_INFO, "READ %i\n", buf_size);
        if (!m_fh) {
            return AVERROR_EOF;
        }
        size_t len = fread(buf, 1, buf_size, m_fh);
        if (len == 0) {
            return AVERROR_EOF;
        }
        return (int)len;
    }

    int64_t seek(int64_t offset, int whence) {
        //log(LOG_INFO, "SEEK %li %i\n", offset, whence);
        if (!m_fh) {
            return -1;
        }
        if (whence == AVSEEK_SIZE) {
            // return the file size if you wish to
            return -1;
        }

        int result = fseek(m_fh, (long)offset, whence);
        if (result != 0) {
            return -1;
        }
        long fpos = ftell(m_fh);
        return (int64_t)fpos;
    }
};
