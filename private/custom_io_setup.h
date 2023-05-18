/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include "../custom_io_group.h"

namespace Avalanche {

void setupInputCustomIoIfNeeded(CustomIoGroup *custom_io_group, AVFormatContext *input_format_context);

}
