/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <unordered_map>
#include <unordered_set>

extern "C" {
#include <libavformat/avformat.h>
}

#include "napi.h"
#include "uv.h"

#include "../private/utils.h"

#include "../custom_io_group.h"
#include "../utils.h"

#include "./resource_io.h"

class ResourceIoGroup : public Avalanche::CustomIoGroup {
public:

    // called in js thread
    ResourceIoGroup(const Napi::Object &resource_io_obj);
    virtual ~ResourceIoGroup();

    void setStopProcessing();

    // called in other threads, directly from libav
    AVIOContext * open(const std::string &uri) override;
    void close(void *opaque) override;

    // called in other threads, directly from libav
    int interruptCallback() override;

    // called in other threads, from ResourceIo
    int read(const std::string &uri, int64_t read_offset, uint8_t *buf, int buf_size);

private:
    Napi::ObjectReference m_resource_io_obj_ref;
    Napi::ThreadSafeFunction m_open_file_func;
    Napi::ThreadSafeFunction m_close_file_func;
    Napi::ThreadSafeFunction m_read_file_func;

    uv_mutex_t m_mutex;
    uv_cond_t m_cond;

    bool m_allow_processing = true;

    std::unordered_set<ResourceIo *> m_resource_ios;
    std::unordered_map<std::string, int64_t> m_file_sizes;

    // called in js thread and other threads
    void lock(std::function<void()> func);

    // called in js thread
    static Napi::Value wrappedOpenFileResolveHandler(const Napi::CallbackInfo &info);
    static Napi::Value wrappedReadFileResolveHandler(const Napi::CallbackInfo &info);

};
