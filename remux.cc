/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>

#include <memory>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

#include "private/av_smart_pointers.h"
#include "private/custom_io_setup.h"
#include "private/stream_map.h"
#include "private/utils.h"

#include "video_reader.h"
#include "remux.h"
#include "utils.h"

using namespace Avalanche;

// this remux sets the start time to be about 0, which is what ffmpeg cli does as well
bool Avalanche::remux(CustomIoGroup *custom_io_group, const std::string &source_uri, const std::string &dest_uri, ProgressFunc progress_func) {

    VideoReader video_reader;

    if (!video_reader.init(custom_io_group, source_uri)) {
        return false;
    }

    if (!video_reader.verifyHasVideoStream()) {
        return false;
    }

    StreamMap &stream_map = video_reader.getStreamMap();

    // setup output

    int ret;

    AVFormatContext *output_format_context_raw = NULL;
    ret = avformat_alloc_output_context2(&output_format_context_raw, NULL, NULL, dest_uri.c_str());
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error allocating output format context %i %s\n", ret, buf);
        return false;
    }
    // put it in a smart pointer to get it properly freed in all cases
    auto output_format_context = std::unique_ptr<AVFormatContext, AVFormatContextOutputCloser>(output_format_context_raw, AVFormatContextOutputCloser());

    if (!stream_map.createOutputStreamsCopyInputFormat(output_format_context.get())) {
        return false;
    }

    //av_dump_format(output_format_context.get(), 0, dest_uri.c_str(), 1);

    // open and initialize output
    ret = avio_open(&output_format_context->pb, dest_uri.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error opening output file %s %i %s\n", dest_uri.c_str(), ret, buf);
        return false;
    }

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "movflags", "faststart", 0);

    ret = avformat_write_header(output_format_context.get(), &opts);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error writing header %i %s\n", ret, buf);
        return false;
    }

    // put it in a smart pointer to get it properly freed in all cases
    auto packet = std::unique_ptr<AVPacket, AVPacketDeleter>(av_packet_alloc(), AVPacketDeleter());
    if (!packet) {
        log(LOG_ERROR, "Error allocating packet\n");
        return false;
    }

    // put it in a smart pointer to get it properly freed in all cases
    auto frame = std::unique_ptr<AVFrame, AVFrameDeleter>(av_frame_alloc(), AVFrameDeleter());
    if (!frame) {
        log(LOG_ERROR, "Error allocating frame\n");
        return false;
    }

    double duration = video_reader.getDuration();
    int total = (int)(ceil(duration * 1.1)); // let the second pass count some progress too
    int prev_step = 0;
    int64_t progress_pts = 0;
    progress_func(prev_step, total);

    while (true) {
        ret = video_reader.readFrame(packet.get());
        if (ret == AVERROR_EOF) {
            log(LOG_INFO, "remux done reading frames\n");
            break;
        }
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            log(LOG_ERROR, "Error reading frame %i %s\n", ret, buf);
            return false;
        }

        // automatically unreference packet at end of loop
        AVPacketUnref packet_unref(packet.get());

        std::shared_ptr<StreamData> stream_data = stream_map.getStreamDataByInputStreamIndex(packet->stream_index);
        if (!stream_data) {
            // not a stream we care about
            continue;
        }
        if (stream_data->base_pts == -1) {
            stream_map.setAllBasePts(stream_data, packet->pts);
        }

        if (stream_data->avmedia_type == AVMEDIA_TYPE_VIDEO) {
            progress_pts = packet->pts;
        }

        if (!stream_map.remuxPacket(packet.get(), output_format_context.get())) {
            return false;
        }

        int step = (int)(progress_pts * av_q2d(stream_map.getVideoAvStream()->time_base));
        if (step > prev_step) {
            progress_func(step, total);
            prev_step = step;
        }
    }
    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    av_write_trailer(output_format_context.get());

    progress_func(total, total);

    stream_map.logStats();

    return true;
};
