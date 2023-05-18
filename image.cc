/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <string.h>

#include "image.h"

using namespace Avalanche;

Image::Image() {
}

Image::~Image() {
}

bool Image::init(int width, int height) {
    setStorage(0, 0, NULL);
    m_storage = nullptr;

    m_storage = std::shared_ptr<uint8_t[]>(new uint8_t[width * height * 3]);
    setStorage(width, height, m_storage.get());

    return true;
}
