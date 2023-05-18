/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <memory>

#include "image_interface.h"

namespace Avalanche {

class Image : public ImageInterface {
    typedef ImageInterface super;
public:
    Image();
    ~Image();

    virtual bool init(int width, int height) override;

private:
    std::shared_ptr<uint8_t[]> m_storage;
};

}
