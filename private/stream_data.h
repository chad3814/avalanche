/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

#include "av_smart_pointers.h"

namespace Avalanche {

struct StreamData {
    StreamData() :
        avmedia_type(AVMEDIA_TYPE_UNKNOWN),
        input_stream_index(-1),
        output_stream_index(-1),
        output_avstream(NULL),
        base_pts(-1),
        output_last_dts(-1),
        count_packets(0),
        count_key_frames(0),
        count_bytes(0),
        output_video_codec(NULL),
        output_video_codec_context(nullptr),
        output_audio_codec(NULL),
        output_audio_codec_context(nullptr),
        output_audio_resampling_context(NULL),
        output_audio_start_pts(-1)
    {
    }

    ~StreamData() {
        // unfortunately this structure (SwrContext) is sized at runtime and
        // we can't wrap it in a shared_ptr.
        if (output_audio_resampling_context) {
            swr_free(&output_audio_resampling_context);
        }
    }

    AVMediaType avmedia_type;
    int input_stream_index;
    int output_stream_index;
    AVStream *output_avstream;

    int64_t base_pts;

    // these are all properties of the _output_. If remuxing, the counts will be the same
    // as the input. If reencoding, they will almost always be different.
    int64_t output_last_dts;
    int64_t count_packets;
    int64_t count_key_frames;
    int64_t count_bytes;

    // this group is used when encoding the output
    const AVCodec *output_video_codec;
    std::shared_ptr<AVCodecContext> output_video_codec_context;
    const AVCodec *output_audio_codec;
    std::shared_ptr<AVCodecContext> output_audio_codec_context;
    SwrContext *output_audio_resampling_context;
    int64_t output_audio_start_pts; // this is in the time base of the output stream

};

}
