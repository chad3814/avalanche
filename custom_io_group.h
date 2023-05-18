/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <string>

extern "C" {
#include <libavformat/avio.h>
}

namespace Avalanche {

class CustomIoGroup {
public:
    virtual ~CustomIoGroup();

    virtual AVIOContext * open(const std::string &url) = 0;
    virtual void close(void *opaque) = 0;

    virtual int interruptCallback() = 0;
};

}
