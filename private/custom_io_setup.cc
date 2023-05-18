/**
 * (c) Chad Walker, Chris Kirmse
 */

#include "../custom_io_group.h"

#include "custom_io_setup.h"

using namespace Avalanche;

int libavOpen(AVFormatContext *format_context, AVIOContext **pb, const char *url, int /*flag*/, AVDictionary **/*options*/) {
    CustomIoGroup *custom_io_group = static_cast<CustomIoGroup *>(format_context->opaque);

    //printf("libavOpen, %s %p\n", url, custom_io_group);

    AVIOContext *avio_context = custom_io_group->open(url);
    if (!avio_context) {
        return AVERROR(ENOENT);
    }

    *pb = avio_context;

    return 0;
}

void libavClose(AVFormatContext *format_context, AVIOContext *pb) {
    CustomIoGroup *custom_io_group = static_cast<CustomIoGroup *>(format_context->opaque);

    //printf("libavClose %p\n", custom_io_group);

    custom_io_group->close(pb->opaque);

    //printf("libavClose %p returning\n", custom_io_group);
}

int libavInterruptCallback(void *opaque)
{
    CustomIoGroup *custom_io_group = static_cast<CustomIoGroup *>(opaque);

    //printf("libavInterruptCallback %p\n", custom_io_group);

    return custom_io_group->interruptCallback();
}

void Avalanche::setupInputCustomIoIfNeeded(CustomIoGroup *custom_io_group, AVFormatContext *input_format_context) {
    if (custom_io_group) {
        input_format_context->flags |= AVFMT_FLAG_CUSTOM_IO;
        input_format_context->opaque = custom_io_group;
        input_format_context->io_open = libavOpen;
        input_format_context->io_close = libavClose;

        input_format_context->interrupt_callback.callback = libavInterruptCallback;
        input_format_context->interrupt_callback.opaque = custom_io_group;
    }
}
