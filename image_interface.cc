/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <string>

#include "image_interface.h"
#include "utils.h"

#include "private/utils.h"

using namespace Avalanche;

ImageInterface::ImageInterface() :
    m_width(0),
    m_height(0) {
}

ImageInterface::~ImageInterface() {
}

bool ImageInterface::savePpm(const std::string &pathname) {
    FILE *fh = fopen(pathname.c_str(), "wb");
    if (!fh) {
        log(LOG_ERROR, "Error opening %s to write image\n", pathname.c_str());
        return false;
    }
    char header[1024];
    int header_size = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", m_width, m_height);
    fwrite(header, header_size, 1, fh);
    fwrite(m_pixels, m_width * m_height * 3, 1, fh);
    fclose(fh);

    return true;
}

void ImageInterface::setStorage(int width, int height, uint8_t *pixels) {
    m_width = width;
    m_height = height;
    m_pixels = pixels;
}
