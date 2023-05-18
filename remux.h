/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <string>

#include "custom_io_group.h"

#include "video_reader.h"

namespace Avalanche {

bool remux(CustomIoGroup *custom_io_group, const std::string &source_uri, const std::string &dest_uri, ProgressFunc progress_func);

}
