/**
 * (c) Chad Walker, Chris Kirmse
 */

#include "resource_io.h"

#include "resource_io_group.h"

using namespace Avalanche;

ResourceIo::ResourceIo(ResourceIoGroup *resource_io_group, const std::string &uri, int64_t file_size) :
    m_resource_io_group(resource_io_group),
    m_uri(uri),
    m_file_size(file_size) {
    int m_len_buffer = 4 * 1024 * 1024;
    if (stringEndsWith(uri, ".m3u8")) {
        // do not need a large buffer for an m3u8
        m_len_buffer = 32 * 1024;
    }
    uint8_t * m_buffer = (unsigned char *)av_malloc(m_len_buffer);
    m_avio_context = avio_alloc_context(m_buffer, m_len_buffer, 0, this, &ResourceIo::libavRead, NULL, &ResourceIo::libavSeek);
}

ResourceIo::~ResourceIo() {
    // we are responsible for this buffer too, though libav internals are allowed
    // to av_realloc it or even free it
    av_freep(&m_avio_context->buffer);
    av_freep(&m_avio_context);
}

int ResourceIo::read(uint8_t *buf, int buf_size) {
    //log(LOG_INFO, "READ %i\n", buf_size);

    if (m_avio_context->pos >= m_file_size) {
        return AVERROR_EOF;
    }

    int res = m_resource_io_group->read(m_uri, m_avio_context->pos, buf, buf_size);
    if (res <= 0) {
        return AVERROR_EOF;
    }

    return res;
}

int64_t ResourceIo::seek(int64_t offset, int whence) {
    //log(LOG_INFO, "SEEK %li %i\n", offset, whence);

    int64_t read_offset = m_avio_context->pos;
    switch (whence) {
    case SEEK_SET:
        //log(LOG_INFO, "SeekSet %zd for %s\n", offset, m_uri.c_str());
        read_offset = offset;
        break;

    case SEEK_CUR:
        //log(LOG_INFO, "SeekCur %zd for %s\n", offset, m_uri.c_str());
        read_offset += offset;
        break;

    case SEEK_END:
        //log(LOG_INFO, "SeekEnd %zd for %s\n", offset, m_uri.c_str());
        read_offset = m_file_size;
        break;

    case AVSEEK_SIZE:
        //log(LOG_INFO, "SeekSize %s, result is %li\n", m_uri.c_str(), m_file_size);
        return m_file_size;

    default:
        log(LOG_ERROR, "Unknown seek whence %i\n", whence);
        return -1;
    }

    if (read_offset < 0) {
        read_offset = 0;
    }
    if (read_offset > m_file_size) {
        read_offset = m_file_size;
    }
    return read_offset;
}
