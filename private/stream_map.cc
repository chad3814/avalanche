/**
 * (c) Chad Walker, Chris Kirmse
 */

extern "C" {
#include <stdint.h>
#include <libavutil/avutil.h>
#include <libavutil/cpu.h>
#include <libavutil/opt.h>
}

#include "../utils.h"

#include "stream_map.h"
#include "utils.h"

using namespace Avalanche;

StreamMap::StreamMap(std::shared_ptr<AVFormatContext> input_format_context) {
    init(input_format_context);
}

StreamMap::StreamMap() {
}

StreamMap::~StreamMap() {
    destroy();
}

void StreamMap::init(std::shared_ptr<AVFormatContext> input_format_context) {
    destroy();

    m_input_format_context = input_format_context;

    for (unsigned int i = 0; i < input_format_context->nb_streams; i++) {
        if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (hasVideo()) {
                continue;
            }

            auto stream_data = std::make_shared<StreamData>();
            stream_data->avmedia_type = AVMEDIA_TYPE_VIDEO;
            stream_data->input_stream_index = i;
            m_map.emplace(i, stream_data);
            continue;
        }
        if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (hasAudio()) {
                continue;
            }

            auto stream_data = std::make_shared<StreamData>();
            stream_data->avmedia_type = AVMEDIA_TYPE_AUDIO;
            stream_data->input_stream_index = i;
            m_map.emplace(i, stream_data);
        }
    }
}

void StreamMap::destroy() {
    //printf("StreamMap::destroy\n");
    m_input_format_context = nullptr;

    m_map.clear();

    m_has_base_pts = false;

    //printf("StreamMap::returning\n");
}

bool StreamMap::hasAudio() const {
    for (auto it: m_map) {
        auto stream_data = it.second;
        if (stream_data->avmedia_type == AVMEDIA_TYPE_AUDIO) {
            return true;
        }
    }
    return false;
}

bool StreamMap::hasVideo() const {
    for (auto it: m_map) {
        auto stream_data = it.second;
        if (stream_data->avmedia_type == AVMEDIA_TYPE_VIDEO) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<StreamData> StreamMap::getVideoStreamData() const {
    for (auto it: m_map) {
        auto stream_data = it.second;
        if (stream_data->avmedia_type == AVMEDIA_TYPE_VIDEO) {
            return stream_data;
        }
    }

    return nullptr;
}

int StreamMap::getVideoInputStreamIndex() const {
    auto stream_data = getVideoStreamData();
    if (!stream_data) {
        return -1;
    }
    return stream_data->input_stream_index;
}

AVStream * StreamMap::getVideoAvStream() const {
    auto stream_data = getVideoStreamData();
    if (!stream_data) {
        return nullptr;
    }
    return m_input_format_context->streams[stream_data->input_stream_index];
}

AVCodecParameters * StreamMap::getVideoAvCodecParameters() const {
    auto input_video_stream = getVideoAvStream();
    if (!input_video_stream) {
        return nullptr;
    }

    return input_video_stream->codecpar;
}

std::shared_ptr<StreamData> StreamMap::getAudioStreamData() const {
    for (auto it: m_map) {
        auto stream_data = it.second;
        if (stream_data->avmedia_type == AVMEDIA_TYPE_AUDIO) {
            return stream_data;
        }
    }

    return nullptr;
}

int StreamMap::getAudioInputStreamIndex() const {
    auto stream_data = getAudioStreamData();
    if (!stream_data) {
        return -1;
    }
    return stream_data->input_stream_index;
}

AVStream * StreamMap::getAudioAvStream() const {
    auto stream_data = getAudioStreamData();
    if (!stream_data) {
        return nullptr;
    }
    return m_input_format_context->streams[stream_data->input_stream_index];
}

AVCodecParameters * StreamMap::getAudioAvCodecParameters() const {
    auto input_audio_stream = getAudioAvStream();
    if (!input_audio_stream) {
        return nullptr;
    }

    return input_audio_stream->codecpar;
}

std::shared_ptr<StreamData> StreamMap::getStreamDataByInputStreamIndex(int stream_index) const {
    for (auto it: m_map) {
        auto stream_data = it.second;
        if (stream_data->input_stream_index == stream_index) {
            return stream_data;
        }
    }
    return nullptr;
}

bool StreamMap::createOutputStreamsCopyInputFormat(AVFormatContext *output_format_context) {
    int i = 0;
    for (auto it: m_map) {
        auto stream_data = it.second;
        //log(LOG_INFO, "input stream %i mapping to output stream %i\n", stream_data->input_stream_index, i);
        stream_data->output_stream_index = i;
        // this gets attached to output_format_context and will be cleaned up by libav
        stream_data->output_avstream = avformat_new_stream(output_format_context, NULL);
        if (!stream_data->output_avstream) {
            log(LOG_ERROR, "Error allocating output stream %i\n", i);
            return false;
        }
        int ret = avcodec_parameters_copy(stream_data->output_avstream->codecpar, m_input_format_context->streams[stream_data->input_stream_index]->codecpar);
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            log(LOG_ERROR, "Error copying codec parameters output stream %i %i %s\n", i, ret, buf);
            return false;
        }
        stream_data->output_avstream->codecpar->codec_tag = 0;

        i++;
    }
    return true;
};

bool StreamMap::createOutputStreamsStandard(AVFormatContext *output_format_context, AVCodecContext *input_audio_codec_context) {
    int i = 0;
    for (auto it: m_map) {
        auto stream_data = it.second;
        //log(LOG_INFO, "input stream %i mapping to output stream %i\n", stream_data->input_stream_index, i);
        stream_data->output_stream_index = i;
        // this gets attached to output_format_context and will be cleaned up by libav
        stream_data->output_avstream = avformat_new_stream(output_format_context, NULL);
        if (!stream_data->output_avstream) {
            log(LOG_ERROR, "Error allocating output stream %i\n", i);
            return false;
        }

        if (stream_data->avmedia_type == AVMEDIA_TYPE_VIDEO) {
            stream_data->output_video_codec = avcodec_find_encoder_by_name("libx264");
            if (!stream_data->output_video_codec) {
                log(LOG_ERROR, "Error finding video encoder");
                return false;
            }

            // use a smart pointer to get it properly freed in all cases
            stream_data->output_video_codec_context = std::shared_ptr<AVCodecContext>(
                avcodec_alloc_context3(stream_data->output_video_codec),
                AVCodecContextDeleter()
                );

            if (!stream_data->output_video_codec_context) {
                log(LOG_ERROR, "Error allocating video codec context\n");
                return false;
            }

            int ret;
            std::string opt_name;

            opt_name = "crf";
            ret = av_opt_set_int(stream_data->output_video_codec_context->priv_data, opt_name.c_str(), 35, 0);
            if (ret < 0) {
                log(LOG_ERROR, "Error setting output video codec option %s %i\n", opt_name.c_str(), ret);
                return false;
            }

            opt_name = "x264-params";
            ret = av_opt_set(stream_data->output_video_codec_context->priv_data, opt_name.c_str(), "force-cfr=1", 0);
            if (ret < 0) {
                log(LOG_ERROR, "Error setting output video codec option %s %i\n", opt_name.c_str(), ret);
                return false;
            }

            stream_data->output_video_codec_context->height = m_input_format_context->streams[stream_data->input_stream_index]->codecpar->height;
            stream_data->output_video_codec_context->width = m_input_format_context->streams[stream_data->input_stream_index]->codecpar->width;
            stream_data->output_video_codec_context->sample_aspect_ratio = m_input_format_context->streams[stream_data->input_stream_index]->codecpar->sample_aspect_ratio;
            // Some video players can only handle YUV420, even though sometimes other formats can be more efficient.
            // We force libav to use it. see https://trac.ffmpeg.org/wiki/Encode/H.264 Encoding for dumb players
            stream_data->output_video_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

            stream_data->output_video_codec_context->time_base.num = 30;
            stream_data->output_video_codec_context->time_base.den = 1;

            stream_data->output_avstream->time_base = stream_data->output_video_codec_context->time_base;

            ret = avcodec_open2(stream_data->output_video_codec_context.get(), stream_data->output_video_codec, NULL);
            if (ret < 0) {
                log(LOG_ERROR, "Error opening output video codec %i\n", ret);
                return false;
            }
            ret = avcodec_parameters_from_context(stream_data->output_avstream->codecpar, stream_data->output_video_codec_context.get());
            if (ret < 0) {
                log(LOG_ERROR, "Error setting output video parameters from context %i\n", ret);
                return false;
            }
        } else {
            stream_data->output_audio_codec = avcodec_find_encoder_by_name("aac");
            if (!stream_data->output_audio_codec) {
                log(LOG_ERROR, "Error finding audio encoder");
                return false;
            }

            // use a smart pointer to get it properly freed in all cases
            stream_data->output_audio_codec_context = std::shared_ptr<AVCodecContext>(
                avcodec_alloc_context3(stream_data->output_audio_codec),
                AVCodecContextDeleter()
                );

            if (!stream_data->output_audio_codec_context) {
                log(LOG_ERROR, "Error allocating audio codec context\n");
                return false;
            }

            stream_data->output_audio_codec_context->channels = 2;
            stream_data->output_audio_codec_context->channel_layout = av_get_default_channel_layout(stream_data->output_audio_codec_context->channels);
            stream_data->output_audio_codec_context->sample_rate = 48000;
            stream_data->output_audio_codec_context->sample_fmt = stream_data->output_audio_codec->sample_fmts[0];
            stream_data->output_audio_codec_context->bit_rate = 196000;
            stream_data->output_audio_codec_context->time_base.num = 1;
            stream_data->output_audio_codec_context->time_base.den = stream_data->output_audio_codec_context->sample_rate;

            stream_data->output_audio_codec_context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

            stream_data->output_avstream->time_base = stream_data->output_audio_codec_context->time_base;

            int ret = avcodec_open2(stream_data->output_audio_codec_context.get(), stream_data->output_audio_codec, NULL);
            if (ret < 0) {
                log(LOG_ERROR, "Error opening audio output codec %i\n", ret);
                return false;
            }

            ret = avcodec_parameters_from_context(stream_data->output_avstream->codecpar, stream_data->output_audio_codec_context.get());
            if (ret < 0) {
                char buf[100];
                av_strerror(ret, buf, sizeof(buf));
                log(LOG_ERROR, "Error setting output audio parameters from context %i %s\n", ret, buf);
                return false;
            }

            // Configure resampling context
            stream_data->output_audio_resampling_context = swr_alloc_set_opts(
                NULL,
                stream_data->output_audio_codec_context->channel_layout,
                stream_data->output_audio_codec_context->sample_fmt,
                stream_data->output_audio_codec_context->sample_rate,
                input_audio_codec_context->channel_layout,
                input_audio_codec_context->sample_fmt,
                input_audio_codec_context->sample_rate,
                0,
                NULL
            );

            if (!stream_data->output_audio_resampling_context) {
                log(LOG_ERROR, "Error allocating output audio resampling context\n");
                return false;
            }

            // Initialize resampling context
            ret = swr_init(stream_data->output_audio_resampling_context);
            if (ret < 0) {
                char buf[100];
                av_strerror(ret, buf, sizeof(buf));
                log(LOG_ERROR, "Error initializing output audio resampling context %i %s\n", ret, buf);
                return false;
            }
        }

        int ret = 0;
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            log(LOG_ERROR, "Error setting up codec parameters output stream %i %i %s\n", i, ret, buf);
            return false;
        }
        stream_data->output_avstream->codecpar->codec_tag = 0;

        i++;
    }
    return true;
}

void StreamMap::setAllBasePts(std::shared_ptr<StreamData> reference_stream_data, int64_t base_pts) {
    m_has_base_pts = true;

    // set the base_pts for the reference stream
    reference_stream_data->base_pts = base_pts;
    auto reference_time_base = m_input_format_context->streams[reference_stream_data->input_stream_index]->time_base;

    // and now set the base_pts for the other stream(s)
    for (auto it: m_map) {
        auto this_stream_data = it.second;
        if (this_stream_data->input_stream_index == reference_stream_data->input_stream_index) {
            continue;
        }
        auto this_time_base = m_input_format_context->streams[this_stream_data->input_stream_index]->time_base;
        this_stream_data->base_pts = av_rescale_q_rnd(reference_stream_data->base_pts, reference_time_base, this_time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
    }
}

double StreamMap::convertTsToSec(std::shared_ptr<StreamData> stream_data, int64_t pts) const {
    return pts * av_q2d(m_input_format_context->streams[stream_data->input_stream_index]->time_base);
}

double StreamMap::getPacketSec(AVPacket *packet) const {
    auto stream_data = getStreamDataByInputStreamIndex(packet->stream_index);
    if (!stream_data) {
        log(LOG_ERROR, "Failed to get stream data for packet stream index %i\n", packet->stream_index);
        return 0;
    }
    return convertTsToSec(stream_data, packet->pts);
}

bool StreamMap::remuxPacket(AVPacket *packet, AVFormatContext *output_format_context) {
    auto stream_data = getStreamDataByInputStreamIndex(packet->stream_index);
    AVStream *input_stream = m_input_format_context->streams[stream_data->input_stream_index];
    AVStream *output_stream = stream_data->output_avstream;

    // modify packet data based on the output

    if (packet->duration < 0) {
        log(LOG_INFO, "packet with negative duration %li, stream index %i dts %li\n", packet->duration, stream_data->input_stream_index, packet->dts);
        packet->duration = 0;
    }

    packet->stream_index = stream_data->output_stream_index;

    //log(LOG_INFO, "remux packet info stream index %i dts %li pts %li\n", stream_data->input_stream_index, packet->dts, packet->pts);
    if (packet->dts == AV_NOPTS_VALUE || packet->pts == AV_NOPTS_VALUE) {
        log(LOG_INFO, "dts or pts not set in a packet for stream %i\n", stream_data->input_stream_index);
        packet->dts = stream_data->output_last_dts + 1;
        packet->pts = packet->dts;
    } else {
        packet->pts = av_rescale_q_rnd(packet->pts - stream_data->base_pts, input_stream->time_base, output_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet->dts = av_rescale_q_rnd(packet->dts - stream_data->base_pts, input_stream->time_base, output_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));

        if (stream_data->count_packets > 0) {
            if (packet->dts <= stream_data->output_last_dts) {
                log(LOG_INFO, "stream %i non-monotonically increasing dts previous %li current %li, changing to %li\n", stream_data->input_stream_index, stream_data->output_last_dts, packet->dts, stream_data->output_last_dts + 1);
                packet->dts = stream_data->output_last_dts + 1;
                if (packet->pts < packet->dts) {
                    packet->pts = packet->dts;
                }
            }
        }
    }
    packet->duration = av_rescale_q(packet->duration, input_stream->time_base, output_stream->time_base);
    // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
    packet->pos = -1;

    // av_interleaved_write_frame zeros out the packet size, so record some stats first
    stream_data->output_last_dts = packet->dts;
    stream_data->count_packets++;
    if (packet->flags == AV_PKT_FLAG_KEY) {
        stream_data->count_key_frames++;
    }
    stream_data->count_bytes += packet->size;

    // write out the packet
    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
    int ret = av_interleaved_write_frame(output_format_context, packet);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error writing frame %i %s\n", ret, buf);
        return false;
    }

    return true;
}

bool StreamMap::encodeVideo(std::shared_ptr<StreamData> stream_data, AVFormatContext *output_format_context, AVFrame *input_frame) {
    if (input_frame) {
        input_frame->pict_type = AV_PICTURE_TYPE_NONE;
        input_frame->pts -= stream_data->base_pts;
    }

    // put it in a smart pointer to get it properly freed in all cases
    auto output_packet = std::unique_ptr<AVPacket, AVPacketUnreferDeleter>(av_packet_alloc(), AVPacketUnreferDeleter());
    if (!output_packet) {
        log(LOG_ERROR, "Error allocating encode video packet\n");
        return false;
    }

    int ret = avcodec_send_frame(stream_data->output_video_codec_context.get(), input_frame);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        printf("Error sending video frame %i %s\n", ret, buf);
        return false;
    }

    while (true) {
        ret = avcodec_receive_packet(stream_data->output_video_codec_context.get(), output_packet.get());
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            break;
        }
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            printf("Error receiving video packet %i %s\n", ret, buf);
            return false;
        }

        output_packet->stream_index = stream_data->output_stream_index;

        AVStream *input_stream = m_input_format_context->streams[stream_data->input_stream_index];
        AVStream *output_stream = stream_data->output_avstream;

        av_packet_rescale_ts(output_packet.get(), input_stream->time_base, output_stream->time_base);

        // av_interleaved_write_frame zeros out the packet size, so record some stats first
        stream_data->output_last_dts = output_packet->dts;
        stream_data->count_packets++;
        if (output_packet->flags == AV_PKT_FLAG_KEY) {
            stream_data->count_key_frames++;
        }
        stream_data->count_bytes += output_packet->size;

        ret = av_interleaved_write_frame(output_format_context, output_packet.get());
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            log(LOG_ERROR, "Error writing video frame %i %s\n", ret, buf);
            return false;
        }
    }

    return true;
}

bool StreamMap::encodeAudio(std::shared_ptr<StreamData> stream_data, AVFormatContext *output_format_context, AVFrame *input_frame) {
    // put it in a smart pointer to get it properly freed in all cases
    auto output_packet = std::unique_ptr<AVPacket, AVPacketUnreferDeleter>(av_packet_alloc(), AVPacketUnreferDeleter());
    if (!output_packet) {
        log(LOG_ERROR, "Error allocating encode audio packet\n");
        return false;
    }

    if (stream_data->output_audio_start_pts < 0) {
        if (!input_frame) {
            // no audio frames processed and now with input_frame == NULL it's trying to drain, so consider that done
            return true;
        }
        AVStream *input_stream = m_input_format_context->streams[stream_data->input_stream_index];
        AVStream *output_stream = stream_data->output_avstream;

        // save the start time of the first audio packet, converted into output time_base
        stream_data->output_audio_start_pts = av_rescale_q_rnd(input_frame->pts - stream_data->base_pts, input_stream->time_base, output_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
    }

    int ret;

    // put it in a smart pointer to get it properly freed in all cases
    auto output_frame = std::unique_ptr<AVFrame, AVFrameDeleter>(av_frame_alloc(), AVFrameDeleter());
    if (!output_frame) {
        log(LOG_ERROR, "Error allocating output audio frame\n");
        return false;
    }

    // channel_layout, sample_rate and format set
    output_frame->channel_layout = stream_data->output_audio_codec_context->channel_layout;
    output_frame->sample_rate = stream_data->output_audio_codec_context->sample_rate;
    output_frame->format = stream_data->output_audio_codec_context->sample_fmt;
    output_frame->nb_samples = stream_data->output_audio_codec_context->frame_size;

    // figure out how many samples we need and allocate the buffers
    ret = av_frame_get_buffer(output_frame.get(), av_cpu_max_align());
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        printf("Error getting output audio frame buffer %i %s\n", ret, buf);
        return false;
    }

    // pass in the decoded input; we don't pass an output, so it will be queued up inside swresample.
    // Then, we loop and get out as many output frames as possible
    ret = swr_convert_frame(stream_data->output_audio_resampling_context, NULL, input_frame);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        printf("Error converting audio frame %i %s\n", ret, buf);
        return false;
    }

    //printf("swresample loop %li\n", swr_get_delay(stream_data->output_audio_resampling_context, stream_data->output_audio_codec_context->sample_rate));
    while (swr_get_delay(stream_data->output_audio_resampling_context, stream_data->output_audio_codec_context->sample_rate) >= output_frame->nb_samples) {
        ret = swr_convert(stream_data->output_audio_resampling_context, output_frame->data, output_frame->nb_samples, NULL, 0);
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            printf("Error converting audio %i %s\n", ret, buf);
            return false;
        }

        ret = avcodec_send_frame(stream_data->output_audio_codec_context.get(), output_frame.get());
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            printf("Error sending audio frame %i %s\n", ret, buf);
            return false;
        }

        while (true) {
            ret = avcodec_receive_packet(stream_data->output_audio_codec_context.get(), output_packet.get());
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                char buf[100];
                av_strerror(ret, buf, sizeof(buf));
                printf("Error receiving audio packet %i %s\n", ret, buf);
                return false;
            }

            output_packet->pts = stream_data->output_audio_start_pts + (stream_data->count_packets * output_frame->nb_samples);
            output_packet->dts = output_packet->pts;
            output_packet->duration = output_frame->nb_samples;

            output_packet->stream_index = stream_data->output_stream_index;

            // av_interleaved_write_frame zeros out the packet size, so record some stats first
            stream_data->output_last_dts = output_packet->dts;
            stream_data->count_packets++;
            stream_data->count_bytes += output_packet->size;

            ret = av_interleaved_write_frame(output_format_context, output_packet.get());
            if (ret < 0) {
                char buf[100];
                av_strerror(ret, buf, sizeof(buf));
                log(LOG_ERROR, "Error writing audio frame %i %s\n", ret, buf);
                return false;
            }
        }
    }

    return true;
}

void StreamMap::logStats() {
    for (auto it: m_map) {
        auto stream_data = it.second;
        log(LOG_INFO, "output stream %i wrote %li packets %li bytes\n", stream_data->output_stream_index, stream_data->count_packets, stream_data->count_bytes);
    }
}
