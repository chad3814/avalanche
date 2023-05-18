/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

#include "custom_io_group.h"
#include "image_interface.h"

#include "private/stream_map.h"
#include "private/packet_queue.h"

namespace Avalanche {

typedef std::function<void(int, int)> ProgressFunc;

struct GetImageResult {
    bool is_eof;
    ImageInterface &image;
    double timestamp;
    double duration;
};

struct GetMetadataResult {
    std::string video_encoding_name;
    std::string audio_encoding_name;

    double container_start_time;
    double container_duration;

    double video_start_time;
    double video_duration;
    int video_width;
    int video_height;
    double frame_rate;
};

struct ExtractClipResult {
    double video_start_time;
    double video_duration;

    int count_video_packets;
    int count_key_frames;
};

struct GetVolumeDataResult {
    // these are in dB
    double mean_volume;
    double max_volume;
};

class VideoReader {
public:
    VideoReader();
    ~VideoReader();

    bool init(CustomIoGroup *custom_io_group, const std::string &uri);
    void destroy();

    bool verifyHasVideoStream();
    bool verifyHasAudioStream();

    StreamMap & getStreamMap() { return m_stream_map; }

    int64_t getDurationPts() { return m_av_format_context->duration; }
    double getDuration() { return ((double)m_av_format_context->duration) / AV_TIME_BASE; }
    int64_t getStartTimePts() { return m_av_format_context->start_time; }
    double getStartTime() { return ((double)m_av_format_context->start_time) / AV_TIME_BASE; }

    int64_t getLatestVideoPts() { return m_latest_video_pts; }
    int64_t getLatestVideoDurationPts() { return m_latest_video_duration_pts; }

    // high level actions
    bool getImageAtTimestamp(double timestamp, GetImageResult &get_image_result);
    bool getMetadata(GetMetadataResult &get_metadata_result);
    bool extractClipReencode(const std::string &dest_uri, double start_time, double end_time, ExtractClipResult &result, ProgressFunc progress_func);
    bool extractClipRemux(const std::string &dest_uri, double start_time, double end_time, ExtractClipResult &result, ProgressFunc progress_func);
    bool remux(const std::string &dest_uri, ExtractClipResult &result, ProgressFunc progress_func);
    bool getClipVolumeData(double start_time, double end_time, GetVolumeDataResult &result, ProgressFunc progress_func);
    bool getVolumeData(GetVolumeDataResult &result, ProgressFunc progress_func);

    // low level actions

    // returns result from av_read_frame but tracks latest video pts and duration
    int readFrame(AVPacket *packet);

    // seeks to the last key frame before or including pts; may leave m_pending_packet_queue
    // with packets (usually does)
    bool safeSeek(int64_t pts, bool &is_eof);

private:
    std::shared_ptr<AVFormatContext> m_av_format_context;

    StreamMap m_stream_map;

    std::shared_ptr<AVCodecContext> m_video_av_codec_context;
    std::shared_ptr<AVCodecContext> m_audio_av_codec_context;

    int64_t m_latest_video_pts = -1;
    int64_t m_latest_video_duration_pts = 0;

    // packets after any asked for time, ready to be read in future calls to high level actions
    // always ends in key frame video packet
    PacketQueue m_pending_packet_queue;

    double convertVideoTsToSec(int64_t ts) { return ts * av_q2d(m_stream_map.getVideoAvStream()->time_base); }
    int64_t convertVideoSecToTs(double sec) { return sec / av_q2d(m_stream_map.getVideoAvStream()->time_base); }

    double convertAudioTsToSec(int64_t ts) { return ts * av_q2d(m_stream_map.getAudioAvStream()->time_base); }
    int64_t convertAudioSecToTs(double sec) { return sec / av_q2d(m_stream_map.getAudioAvStream()->time_base); }

    bool initVideoCodecContext();
    bool initAudioCodecContext();

    bool readAndGetImage(int64_t pts, GetImageResult &get_image_result);
};

}
