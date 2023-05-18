/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace Avalanche {

struct AVFormatContextInputCloser {
    // called by smart ptr to destroy/free the resource
    void operator()(AVFormatContext *input_format_context) {
        //printf("cleaning up av_format_context %p closer\n", input_format_context);
        AVFormatContext *local = input_format_context;
        avformat_close_input(&local);
    }
};

struct AVFormatContextDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(AVFormatContext *format_context) {
        //printf("cleaning up av_format_context %p deleter\n", format_context);
        avformat_free_context(format_context);
    }
};

struct AVFormatContextOutputCloser {
    // called by smart ptr to destroy/free the resource
    void operator()(AVFormatContext *output_format_context) {
        //printf("cleaning up av_format_context %p\n", output_format_context);
        if (output_format_context->pb && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }
        avformat_free_context(output_format_context);
    }
};

struct AVCodecContextDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(AVCodecContext *codec_context) {
        //printf("cleaning up codec_context %p\n", codec_context);
        AVCodecContext *local = codec_context;
        avcodec_free_context(&local);
    }
};

struct AVPacketDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(AVPacket *packet) {
        //printf("cleaning up packet %p\n", packet);
        AVPacket *local = packet;
        av_packet_free(&local);
    }
};

struct AVPacketUnreferDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(AVPacket *packet) {
        //printf("cleaning up packet %p\n", packet);
        AVPacket *local = packet;
        av_packet_unref(packet);
        av_packet_free(&local);
    }
};

struct AVPacketUnref {
    AVPacketUnref(AVPacket *packet): packet(packet) {}

    ~AVPacketUnref() {
        av_packet_unref(packet);
    }

    AVPacket *packet;
};

struct AVFrameDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(AVFrame *frame) {
        //printf("cleaning up frame %p\n", frame);
        AVFrame *local = frame;
        av_frame_free(&local);
    }
};

struct AVFrameUnref {
    AVFrameUnref(AVFrame *frame): frame(frame) {}

    ~AVFrameUnref() {
        av_frame_unref(frame);
    }

    AVFrame *frame;
};

struct SwsContextDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(SwsContext *sws_context) {
        //printf("cleaning up sws_context %p\n", sws_context);
        sws_freeContext(sws_context);
    }
};

struct SwrContextDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(SwrContext *swr_context) {
        //printf("cleaning up swr_context %p\n", swr_context);
        SwrContext *local = swr_context;
        swr_free(&local);
    }
};

struct AVRawDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(uint8_t *raw) {
        //printf("cleaning up raw %p\n", raw);
        av_free(raw);
    }
    // called by smart ptr to destroy/free the resource
    void operator()(int16_t *raw) {
        //printf("cleaning up raw %p\n", raw);
        av_free((uint8_t *)raw);
    }
    // called by smart ptr to destroy/free the resource
    void operator()(float *raw) {
        //printf("cleaning up raw %p\n", raw);
        av_free((uint8_t *)raw);
    }
};

// this is specifically called audio array deleter because in audio-land only the first element of the array
// needs to be freed
struct AVAudioArrayDeleter {
    // called by smart ptr to destroy/free the resource
    void operator()(float **raw) {
        if (raw) {
            av_free((uint8_t *)raw[0]);
            av_free(raw);
        }
        //printf("cleaning up raw %p\n", raw);
    }
};

}
