/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <map>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
}

#include "stream_data.h"

namespace Avalanche {

class StreamMap {
public:

    StreamMap(std::shared_ptr<AVFormatContext> input_format_context);
    StreamMap();
    ~StreamMap();

    void init(std::shared_ptr<AVFormatContext> input_format_context);
    void destroy();

    bool hasAudio() const;
    bool hasVideo() const;

    // guaranteed to return non-null iff hasVideo() is true
    std::shared_ptr<StreamData> getVideoStreamData() const;
    // guaranteed to return >= 0 iff hasVideo() is true
    int getVideoInputStreamIndex() const;
    // guaranteed to return non-null iff hasVideo() is true
    AVStream * getVideoAvStream() const;
    // guaranteed to return non-null iff hasVideo() is true
    AVCodecParameters * getVideoAvCodecParameters() const;

    // guaranteed to return non-null iff hasVideo() is true
    std::shared_ptr<StreamData> getAudioStreamData() const;
    // guaranteed to return >= 0 iff hasVideo() is true
    int getAudioInputStreamIndex() const;
    // guaranteed to return non-null iff hasAudio() is true
    AVStream * getAudioAvStream() const;
    // guaranteed to return non-null iff hasVideo() is true
    AVCodecParameters * getAudioAvCodecParameters() const;

    std::shared_ptr<StreamData> getStreamDataByInputStreamIndex(int stream_index) const;

    bool createOutputStreamsCopyInputFormat(AVFormatContext *output_format_context);
    bool createOutputStreamsStandard(AVFormatContext *output_format_context, AVCodecContext *audio_codec_context);

    bool hasBasePts() const { return m_has_base_pts; }
    void setAllBasePts(std::shared_ptr<StreamData> reference_stream_data, int64_t base_pts);

    double convertTsToSec(std::shared_ptr<StreamData> stream_data, int64_t pts) const;
    double getPacketSec(AVPacket *packet) const;

    bool remuxPacket(AVPacket *packet, AVFormatContext *output_format_context);

    bool encodeVideo(std::shared_ptr<StreamData> stream_data, AVFormatContext *output_format_context, AVFrame *input_frame);
    bool encodeAudio(std::shared_ptr<StreamData> stream_data, AVFormatContext *output_format_context, AVFrame *input_frame);

    void logStats();

private:

    std::shared_ptr<AVFormatContext> m_input_format_context;

    // from input stream index to a full blob of stream data used for muxing, encoding, etc.
    std::map<int, std::shared_ptr<StreamData>> m_map;

    bool m_has_base_pts = false;

};

}
