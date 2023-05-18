/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include "uv.h"

class UvMutexLock {
public:
    UvMutexLock(uv_mutex_t &uv_mutex) :
        m_mutex(uv_mutex) {
        uv_mutex_lock(&m_mutex);
    }

    ~UvMutexLock() {
        uv_mutex_unlock(&m_mutex);
    }

private:
    uv_mutex_t &m_mutex;
};
