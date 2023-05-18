/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include "napi.h"
#include "uv.h"

#include "../image_interface.h"

class BufferImage : public Avalanche::ImageInterface {
    typedef ImageInterface super;
public:
    BufferImage(Napi::Env env);
    ~BufferImage();

    virtual bool init(int width, int height) override;
    void drain();

    Napi::Reference<Napi::Buffer<uint8_t>> & getBufferRef() { return m_buffer_ref; }

private:
    Napi::ThreadSafeFunction m_alloc_buffer_func;

    uv_mutex_t m_mutex;
    uv_cond_t m_cond;

    Napi::Buffer<uint8_t> m_buffer;

    bool m_is_draining;

    // need to reference the buffer to make sure it's not freed before we return it
    Napi::Reference<Napi::Buffer<uint8_t>> m_buffer_ref;

    // called in js thread and other threads
    void lock(std::function<void()> func);

};
