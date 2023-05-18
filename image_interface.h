/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <stdint.h>
#include <string.h>

#include <string>

// this stores an image in memory in RGB order, 8 bits per color, top left to bottom right, no gaps between rows

namespace Avalanche {

class ImageInterface {
public:
    ImageInterface();
    ~ImageInterface();

    virtual bool init(int width, int height) = 0;

    bool isInitialized() { return m_width > 0 && m_height > 0; }
    int getWidth() { return m_width; }
    int getHeight() { return m_height; }

    void setRow(int y, uint8_t *src) { memcpy(m_pixels + (y * m_width * 3), src, m_width * 3); }

    bool savePpm(const std::string &pathname);

protected:
    void setStorage(int width, int height, uint8_t *pixels);

private:
    int m_width;
    int m_height;
    uint8_t *m_pixels;
};

}
