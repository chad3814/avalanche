/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <string.h>

#include "../utils.h"
#include "../uv_mutex_lock.h"

#include "../private/utils.h"

#include "buffer_image.h"

using namespace Avalanche;

BufferImage::BufferImage(Napi::Env env):
    m_is_draining(false) {
    auto finalizer_alloc_buffer = [](const Napi::Env &) {};
    Napi::Function fake_js_func; // we just need to run some c++ code in main thread, not js code, so we don't need a real js func
    m_alloc_buffer_func = Napi::ThreadSafeFunction::New(env, fake_js_func, "alloc_buffer_func", 0, 1, finalizer_alloc_buffer);
    m_alloc_buffer_func.Unref(env);

    int ret;

    ret = uv_mutex_init(&m_mutex);
    if (ret != 0) {
        log(LOG_ERROR, "BufferImage UvMutexInitFailed %i", ret);
        return;
    }
    ret = uv_cond_init(&m_cond);
    if (ret != 0) {
        log(LOG_ERROR, "BufferImage UvCondInitFailed %i", ret);
        return;
    }
}

BufferImage::~BufferImage() {
    //printf("BufferImage::~BufferImage\n");

    m_alloc_buffer_func.Release();

    uv_mutex_destroy(&m_mutex);
    uv_cond_destroy(&m_cond);
}

bool BufferImage::init(int width, int height) {
    napi_status status;

    //printf("BufferImage::init\n");

    if (m_is_draining) {
        return false;
    }

    status = m_alloc_buffer_func.Acquire();
    if (status != napi_ok) {
        printf("failed to acquire alloc buffer %i\n", status);
        return false;
    }

    bool is_done = false;
    char header[1024];
    int header_size = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", width, height);

    status = m_alloc_buffer_func.BlockingCall([this, width, height, &is_done, header_size](const Napi::Env &env, const Napi::Function &) {
        // this code is run in the main js thread
        Napi::HandleScope scope(env);

        m_buffer = Napi::Buffer<uint8_t>::New(env, width * height * 3 + header_size);
        // create a reference to it so that the buffer won't be garbage collected until our owner is ready for it
        m_buffer_ref = Napi::Reference<Napi::Buffer<uint8_t>>::New(m_buffer, 1);

        lock([&is_done]() {
            is_done = true;
        });

        uv_cond_signal(&m_cond);
    });

    if (status != napi_ok) {
        printf("failed to call js_func to alloc buffer\n");
        if (status == napi_closing) {
            return false;
        }
    }

    status = m_alloc_buffer_func.Release();
    if (status != napi_ok) {
        printf("failed to release alloc buffer %i\n", status);
        return false;
    }

    lock([this, &is_done]() {
        while (!is_done && !m_is_draining) {
            uv_cond_wait(&m_cond, &m_mutex);
        }
    });

    if (m_is_draining) {
        printf("BufferImage::init reporting failure due to draining\n");
        return false;
    }

    memcpy(m_buffer.Data(), header, header_size);
    // point the storage of the image to be after the net_image_buffer (aka ppm) header
    setStorage(width, height, m_buffer.Data() + header_size);

    //printf("BufferImage::init returning\n");

    return true;
}

void BufferImage::drain() {
    m_is_draining = true;
    uv_cond_signal(&m_cond);
}

void BufferImage::lock(std::function<void()> func) {
    UvMutexLock lock(m_mutex);

    func();
}
