/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <memory>

extern "C" {
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/cpu.h>
}

#include "video_reader.h"

#include "utils.h"

#include "private/av_smart_pointers.h"
#include "private/custom_io_setup.h"
#include "private/packet_queue.h"
#include "private/utils.h"
#include "private/volume_data.h"

constexpr double MAX_LOOK_PAST_TIME_SEC = 5.;

using namespace Avalanche;

VideoReader::VideoReader() {
}

VideoReader::~VideoReader() {
    destroy();
}

bool VideoReader::init(CustomIoGroup *custom_io_group, const std::string &uri) {
    int ret;

    AVFormatContext *input_format_context_raw = avformat_alloc_context();
    if (!input_format_context_raw) {
        log(LOG_ERROR, "Error allocating input format context\n");
        return false;
    }
    input_format_context_raw->protocol_whitelist = av_strdup("file,https,tcp,tls");

    setupInputCustomIoIfNeeded(custom_io_group, input_format_context_raw);

    AVDictionary *opts = NULL;
    if (stringEndsWith(uri, std::string(".m3u8"))) {
        // always start at the beginning of the video
        av_dict_set_int(&opts, "live_start_index", 0, 0);
        // if m3u8 is unchanged 25 times in a row, consider it end of file
        av_dict_set_int(&opts, "m3u8_hold_counters", 100, 0);
        // do reads of different files at the same time (if I read hls.c in libav correctly)
        av_dict_set_int(&opts, "http_multiple", 1, 0);
    }

    ret = avformat_open_input(&input_format_context_raw, uri.c_str(), NULL, &opts);
    if (ret < 0) {
        avformat_free_context(input_format_context_raw);
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error opening %s %i %s\n", uri.c_str(), ret, buf);
        return false;
    }
    // avformat_open_input returns us back a dictionary with any invalid options
    if (av_dict_count(opts) > 0) {
        printf("invalid option passed to avformat_open_input\n");
    }

    // put it in a smart pointer to get it properly closed in all cases
    m_av_format_context = std::shared_ptr<AVFormatContext>(input_format_context_raw, AVFormatContextInputCloser());

    // hls (ts files really) don't have any headers, so libav needs to read through the stream a bit to
    // extract metadata that is periodically placed in there. The default is fine for good videos, but
    // messed up videos need some extra time to find the metadata
    m_av_format_context->max_analyze_duration = 60 * AV_TIME_BASE;

    ret = avformat_find_stream_info(m_av_format_context.get(), NULL);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error retrieving input stream information %i %s\n", ret, buf);
        return false;
    }

    //av_dump_format(input_format_context.get(), 0, source_uri.c_str(), 0);

    m_stream_map.init(m_av_format_context);

    return true;
}

void VideoReader::destroy() {
    //printf("VideoReader::destroy\n");

    m_video_av_codec_context = nullptr;
    m_audio_av_codec_context = nullptr;

    m_av_format_context = nullptr;

    m_stream_map.destroy();

    m_latest_video_pts = -1;
    m_latest_video_duration_pts = 0;

    m_pending_packet_queue.clear();

    //printf("VideoReader::destroy returning\n");
}

bool VideoReader::verifyHasVideoStream() {
    if (!m_stream_map.hasVideo()) {
        log(LOG_INFO, "no video stream found\n");
        return false;
    }
    return true;
}

bool VideoReader::verifyHasAudioStream() {
    if (!m_stream_map.hasAudio()) {
        log(LOG_INFO, "no audio stream found\n");
        return false;
    }
    return true;
}

bool VideoReader::getImageAtTimestamp(double timestamp, GetImageResult &get_image_result) {
    //log(LOG_INFO, "trying to get image at timestamp %f\n", timestamp);

    if (!initVideoCodecContext()) {
        return false;
    }

    int64_t desired_pts = convertVideoSecToTs(timestamp);

    //log(LOG_INFO, "get image at timestamp desired pts %li\n", desired_pts);

    AVStream *stream = m_stream_map.getVideoAvStream();
    int64_t video_start_time_ts = stream->start_time;
    int64_t video_duration_ts = stream->duration;

    if (video_start_time_ts != AV_NOPTS_VALUE && video_duration_ts != AV_NOPTS_VALUE) {
        if (desired_pts > video_start_time_ts + video_duration_ts) {
            log(LOG_INFO, "asked to get image at timestamp after end of video stream %li %li\n", desired_pts, video_start_time_ts + video_duration_ts);
            get_image_result.is_eof = true;
            return false;
        }
    }

    // seek to timestamp if needed, or read to timestamp

    PacketQueue packet_queue;

    double latest_video_timestamp = convertVideoTsToSec(m_latest_video_pts);
    if (timestamp - latest_video_timestamp > 11) {
        bool is_eof = false;
        if (!safeSeek(desired_pts, is_eof)) {
            if (is_eof) {
                get_image_result.is_eof = true;
            }
            return false;
        }
    }

    if (desired_pts < video_start_time_ts) {
        desired_pts = video_start_time_ts;
    }
    return readAndGetImage(desired_pts, get_image_result);
}

bool VideoReader::getMetadata(GetMetadataResult &get_metadata_result) {
    if (!m_av_format_context) {
        return false;
    }
    double start_time = getStartTime();
    double duration = getDuration();

    get_metadata_result.container_start_time = start_time;
    get_metadata_result.container_duration = duration;
    get_metadata_result.video_start_time = 0;
    get_metadata_result.video_duration = 0;
    get_metadata_result.video_width = 0;
    get_metadata_result.video_height = 0;
    get_metadata_result.frame_rate = 0;

    if (m_stream_map.hasVideo()) {
        if (!initVideoCodecContext()) {
            return false;
        }

        get_metadata_result.video_encoding_name = std::string(m_video_av_codec_context->codec->name);

        AVStream *stream = m_stream_map.getVideoAvStream();
        AVRational avg_frame_rate = stream->avg_frame_rate;
        double frame_rate = av_q2d(avg_frame_rate);
        if (isnan(frame_rate)) {
            frame_rate = 0;
        }

        int video_width = m_video_av_codec_context->width;
        int video_height = m_video_av_codec_context->height;

        int64_t video_start_time_ts = stream->start_time;
        double video_start_time = convertVideoTsToSec(video_start_time_ts);
        int64_t video_duration_ts = stream->duration;
        double video_duration = convertVideoTsToSec(video_duration_ts);

        //log(LOG_INFO, "dimensions %i %i\n", video_width, video_height);
        //log(LOG_INFO, "video start time %f\n", video_start_time);
        //log(LOG_INFO, "video duration %f\n", video_duration);

        get_metadata_result.video_start_time = video_start_time;
        get_metadata_result.video_duration = video_duration;
        get_metadata_result.video_width = video_width;
        get_metadata_result.video_height = video_height;
        get_metadata_result.frame_rate = frame_rate;
    }

    if (m_stream_map.hasAudio()) {
        if (!initAudioCodecContext()) {
            return false;
        }
        get_metadata_result.audio_encoding_name = std::string(m_audio_av_codec_context->codec->name);
    }

    return true;
}

bool VideoReader::extractClipReencode(const std::string &dest_uri, double start_time, double end_time, ExtractClipResult &result, ProgressFunc progress_func) {
    if (end_time < start_time) {
        log(LOG_ERROR, "Invalid end time %f before start time %f\n", end_time, start_time);
        return false;
    }
    if (!initVideoCodecContext()) {
        return false;
    }

    if (m_stream_map.hasAudio()) {
        if (!initAudioCodecContext()) {
            return false;
        }
    }

    // setup output

    m_stream_map.init(m_av_format_context);

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

    if (!m_stream_map.createOutputStreamsStandard(output_format_context.get(), m_audio_av_codec_context.get())) {
        return false;
    }

    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        output_format_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    av_dump_format(output_format_context.get(), 0, dest_uri.c_str(), 1);

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
        printf("Error allocating frame\n");
        return false;
    }

    double duration = end_time - start_time;
    int total = (int)(ceil(duration + 2)); // let the seek and draining each count a step too
    int prev_step = 0;
    int64_t progress_pts = 0;
    progress_func(prev_step, total);

    // loop through reading all the packets and reencode
    int64_t desired_start_pts = convertVideoSecToTs(start_time);

    bool is_eof = false;
    if (!safeSeek(desired_start_pts, is_eof)) {
        return false;
    }

    prev_step++;
    progress_func(prev_step, total);

    // remember, safeSeek only got us to the latest key frame before the desired_start_pts!
    // so we need to process some packets before encoding the output
    bool is_encoding_started = false;

    bool is_done = false;
    while (!is_done) {
        ret = readFrame(packet.get());
        if (ret == AVERROR_EOF) {
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

        std::shared_ptr<StreamData> stream_data = m_stream_map.getStreamDataByInputStreamIndex(packet->stream_index);
        if (!stream_data) {
            // not a stream we care about
            continue;
        }

        if (packet->stream_index == m_stream_map.getVideoInputStreamIndex()) {
            if (convertVideoTsToSec(packet->pts) > end_time) {
                is_done = true;
            }
        }

        if (packet->stream_index == m_stream_map.getVideoInputStreamIndex()) {
            progress_pts = packet->pts;

            if (!is_encoding_started && packet->pts + packet->duration > desired_start_pts) {
                //printf("starting encoding with pts %li desired start was %li\n", packet->pts, desired_start_pts);
                is_encoding_started = true;
                m_stream_map.setAllBasePts(stream_data, packet->pts);
            }

            if (!is_encoding_started) {
                continue;
            }

            int ret = avcodec_send_packet(m_video_av_codec_context.get(), packet.get());
            if (ret < 0) {
                if (ret != AVERROR(EAGAIN)) {
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    log(LOG_ERROR, "Error sending packet to video codec context %i %s\n", ret, buf);
                    return false;
                }
            }

            while (true) {
                ret = avcodec_receive_frame(m_video_av_codec_context.get(), frame.get());
                if (ret < 0) {
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    printf("Error receiving video frame %i %s\n", ret, buf);
                    return false;
                }

                // automatically unreference frame at end of loop
                AVFrameUnref frame_unref(frame.get());

                if (!m_stream_map.encodeVideo(stream_data, output_format_context.get(), frame.get())) {
                    return false;
                }
            }
        }

        if (!is_encoding_started) {
            continue;
        }
        if (packet->stream_index == m_stream_map.getAudioInputStreamIndex()) {
            int ret = avcodec_send_packet(m_audio_av_codec_context.get(), packet.get());
            if (ret < 0) {
                if (ret != AVERROR(EAGAIN)) {
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    log(LOG_ERROR, "Error sending packet to audio codec context %i %s\n", ret, buf);
                    return false;
                }
            }

            while (true) {
                ret = avcodec_receive_frame(m_audio_av_codec_context.get(), frame.get());
                if (ret < 0) {
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    printf("Error receiving audio frame %i %s\n", ret, buf);
                    return false;
                }

                // automatically unreference frame at end of loop
                AVFrameUnref frame_unref(frame.get());

                if (!m_stream_map.encodeAudio(stream_data, output_format_context.get(), frame.get())) {
                    return false;
                }
            }
        }

        int step = (int)(1 + convertVideoTsToSec(progress_pts) - start_time);
        if (step > prev_step) {
            progress_func(step, total);
            prev_step = step;
        }
    }

    // drain any last frames of video and audio
    if (!m_stream_map.encodeVideo(m_stream_map.getVideoStreamData(), output_format_context.get(), NULL)) {
        return false;
    }
    if (m_stream_map.hasAudio()) {
        if (!m_stream_map.encodeAudio(m_stream_map.getAudioStreamData(), output_format_context.get(), NULL)) {
            return false;
        }
    }

    av_write_trailer(output_format_context.get());

    progress_func(total, total);

    auto video_stream_data = m_stream_map.getVideoStreamData();
    double output_start_time = video_stream_data->base_pts * av_q2d(m_stream_map.getVideoAvStream()->time_base);
    double output_duration = (getLatestVideoPts() - getLatestVideoDurationPts() - video_stream_data->base_pts) * av_q2d(m_stream_map.getVideoAvStream()->time_base);

    m_stream_map.logStats();

    result.count_video_packets = video_stream_data->count_packets;
    result.count_key_frames = video_stream_data->count_key_frames;
    result.video_start_time = output_start_time;
    result.video_duration = output_duration;

    return true;
}

bool VideoReader::extractClipRemux(const std::string &dest_uri, double start_time, double end_time, ExtractClipResult &result, ProgressFunc progress_func) {
    if (end_time < start_time) {
        log(LOG_ERROR, "Invalid end time %f before start time %f\n", end_time, start_time);
        return false;
    }

    if (!verifyHasVideoStream()) {
        return false;
    }

    // setup output

    m_stream_map.init(m_av_format_context);

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

    if (!m_stream_map.createOutputStreamsCopyInputFormat(output_format_context.get())) {
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

    int64_t desired_start_pts = convertVideoSecToTs(start_time);

    bool is_eof = false;
    if (!safeSeek(desired_start_pts, is_eof)) {
        return false;
    }

    double progress_base_time = start_time;
    if (!m_pending_packet_queue.isEmpty()) {
        progress_base_time = m_stream_map.getPacketSec(m_pending_packet_queue.getFirst().get());
    }
    double duration = end_time - progress_base_time;
    int total = (int)(ceil(duration + 2)); // let the seek (which we already did) and draining each count a step too
    int prev_step = 1;
    int64_t progress_pts = 0;
    progress_func(prev_step, total);


    bool is_done = false;
    while (!is_done) {
        ret = readFrame(packet.get());
        if (ret == AVERROR_EOF) {
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

        std::shared_ptr<StreamData> stream_data = m_stream_map.getStreamDataByInputStreamIndex(packet->stream_index);
        if (!stream_data) {
            // not a stream we care about
            continue;
        }

        if (!m_stream_map.hasBasePts()) {
            m_stream_map.setAllBasePts(stream_data, packet->pts);
        }

        if (packet->stream_index == m_stream_map.getVideoInputStreamIndex()) {
            progress_pts = packet->pts;

            // need to read packet->pts before remuxing since remuxing modifies them
            if (convertVideoTsToSec(packet->pts) > end_time) {
                is_done = true;
            }
        }

        if (!m_stream_map.remuxPacket(packet.get(), output_format_context.get())) {
            return false;
        }

        int step = (int)(1 + convertVideoTsToSec(progress_pts) - progress_base_time);
        if (step > prev_step) {
            progress_func(step, total);
            prev_step = step;
        }
    }

    auto video_stream_data = m_stream_map.getVideoStreamData();
    double output_start_time = convertVideoTsToSec(video_stream_data->base_pts);
    double output_duration = convertVideoTsToSec(getLatestVideoPts() - getLatestVideoDurationPts() - video_stream_data->base_pts);

    m_stream_map.logStats();

    result.count_video_packets = video_stream_data->count_packets;
    result.count_key_frames = video_stream_data->count_key_frames;
    result.video_start_time = output_start_time;
    result.video_duration = output_duration;

    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    av_write_trailer(output_format_context.get());

    progress_func(total, total);

    return true;
}

bool VideoReader::remux(const std::string &dest_uri, ExtractClipResult &result, ProgressFunc progress_func) {
    if (!initVideoCodecContext()) {
        return false;
    }

    // HLS inputs don't have a video stream duration, so we just use container duration with a little fudge factor
    // (sigh about needing the fudge factor)

    double end_time = std::max((double)0, getStartTime()) + getDuration() + 1;

    return extractClipRemux(dest_uri, 0, end_time, result, progress_func);
}

bool VideoReader::getClipVolumeData(double start_time, double end_time, GetVolumeDataResult &result, ProgressFunc progress_func) {
    if (end_time < start_time) {
        log(LOG_ERROR, "Invalid end time %f before start time %f\n", end_time, start_time);
        return false;
    }
    if (!initAudioCodecContext()) {
        log(LOG_ERROR, "failed to init audio codec context\n");
        return false;
    }

    // We want our audio sample data as floats (range -1 to 1) because that's what volume_data is expecting.
    // as of 2021-march-21 the aac decoder in ffmpeg always returns its samples in floats (AV_SAMPLE_FMT_FLTP)
    // but that's not guaranteed and other decoders might not do that. We do this conversion step just in case.

    // we use the requested channel layout (which we got from the stream metadata) because after avcodec_open2
    // sometimes it changes the channel_layout to something it is not (ultraclip 181153471 is mono but
    // after avcodec_open2 channel_layout is set to 3 [stereo] when it should be 4 [mono center]).
    // put it in a smart pointer to get it properly freed in all cases
    auto swr_context = std::unique_ptr<SwrContext, SwrContextDeleter>(
        swr_alloc_set_opts(
            NULL,
            m_audio_av_codec_context->request_channel_layout,
            AV_SAMPLE_FMT_FLTP,
            m_audio_av_codec_context->sample_rate,
            m_audio_av_codec_context->request_channel_layout,
            m_audio_av_codec_context->sample_fmt,
            m_audio_av_codec_context->sample_rate,
            0,
            NULL
            ),
        SwrContextDeleter()
        );
    if (!swr_context) {
        log(LOG_ERROR, "failed to alloc swr context\n");
        return false;
    }

    int ret = swr_init(swr_context.get());
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error initializing resampling context %i %s\n", ret, buf);
        return false;
    }

    auto expected_channels = m_audio_av_codec_context->channels;

    // put it in a smart pointer to get it properly freed in all cases
    auto packet = std::unique_ptr<AVPacket, AVPacketDeleter>(av_packet_alloc(), AVPacketDeleter());
    if (!packet) {
        log(LOG_ERROR, "Error allocating packet\n");
        return false;
    }

    // put it in a smart pointer to get it properly freed in all cases
    auto frame = std::unique_ptr<AVFrame, AVFrameDeleter>(av_frame_alloc(), AVFrameDeleter());
    if (!frame) {
        printf("Error allocating frame\n");
        return false;
    }

    double duration = end_time - start_time;
    int total = (int)(ceil(duration + 2)); // let the seek and draining each count a step too
    int prev_step = 0;
    int64_t progress_pts = 0;
    progress_func(prev_step, total);

    // loop through reading all the packets and reencode
    int64_t desired_start_pts = convertAudioSecToTs(start_time);

    // we're not processing the video stream so no need to use safeSeek
    ret = av_seek_frame(m_av_format_context.get(), m_stream_map.getAudioInputStreamIndex(), desired_start_pts, AVSEEK_FLAG_BACKWARD);
    // seeking that fails mostly return -1, which in libav is equivalent to EPERM, but it's not actually
    // a permission issue; it's just used as a generic error (such as trying to seek outside the video)
    // this was discovered by reading the av_seek_frame code.
    if (ret == -1 || ret == AVERROR_EOF) {
        log(LOG_ERROR, "failed to seek %i\n", ret);
        return false;
    }
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error seeking %i %s\n", ret, buf);
        return false;
    }

    prev_step++;
    progress_func(prev_step, total);

    VolumeData volume_data;

    bool is_done = false;
    while (!is_done) {
        ret = readFrame(packet.get());
        if (ret == AVERROR_EOF) {
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

        std::shared_ptr<StreamData> stream_data = m_stream_map.getStreamDataByInputStreamIndex(packet->stream_index);
        if (!stream_data) {
            // not a stream we care about
            continue;
        }

        if (packet->stream_index == m_stream_map.getAudioInputStreamIndex()) {
            if (convertAudioTsToSec(packet->pts) > end_time) {
                is_done = true;
            }
            progress_pts = packet->pts;

            int ret = avcodec_send_packet(m_audio_av_codec_context.get(), packet.get());
            if (ret < 0) {
                if (ret != AVERROR(EAGAIN)) {
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    log(LOG_ERROR, "Error sending packet to audio codec context %i %s\n", ret, buf);
                    return false;
                }
            }

            while (true) {
                ret = avcodec_receive_frame(m_audio_av_codec_context.get(), frame.get());
                if (ret < 0) {
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    printf("Error receiving audio frame %i %s\n", ret, buf);
                    return false;
                }

                // automatically unreference frame at end of loop
                AVFrameUnref frame_unref(frame.get());

                //log(LOG_INFO, "samples %i channels %i\n", frame->nb_samples, frame->channels);

                if (frame->channels != expected_channels) {
                    // we have seen some facebook videos sometimes have frames with only 1 channel when most of the video has 2
                    // those frames cause problems because swr_convert reads the frame as if it had 2, and so causes an access violation
                    //log(LOG_INFO, "skipping frame with unexpected number of channels %i\n", frame->channels);
                    continue;
                }
                float **output_buffer_raw = NULL;
                // we use swr_get_out_samples() to know how many samples to allocate so we know we only need to call swr_convert once
                int linesize;
                ret = av_samples_alloc_array_and_samples((uint8_t ***)&output_buffer_raw, &linesize, frame->channels, swr_get_out_samples(swr_context.get(), frame->nb_samples), AV_SAMPLE_FMT_FLTP, 0);
                if (ret < 0) {
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    printf("Error allocating output buffers for resampling %i %s\n", ret, buf);
                    return false;
                }
                auto output_buffer = std::unique_ptr<float *, AVAudioArrayDeleter>(output_buffer_raw, AVAudioArrayDeleter());
                ret = swr_convert(swr_context.get(), (uint8_t **)output_buffer_raw, frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
                if (ret < 0) {
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    printf("Error converting audio to desired format %i %s\n", ret, buf);
                    return false;
                }

                int num_output_samples = ret;

                // we converted to AV_SAMPLE_FMT_FLTP which is planar--this means we have one array to process per channel
                for (int i = 0; i< frame->channels; i++) {
                    volume_data.addSamples(output_buffer_raw[i], num_output_samples);
                }
            }
        }

        int step = (int)(1 + convertAudioTsToSec(progress_pts) - start_time);
        if (step > prev_step) {
            progress_func(step, total);
            prev_step = step;
        }
    }

    progress_func(total, total);

    volume_data.calculateResults(result);

    return true;
}

bool VideoReader::getVolumeData(GetVolumeDataResult &result, ProgressFunc progress_func) {
    if (!initAudioCodecContext()) {
        return false;
    }
    // HLS inputs don't have a video stream duration, so we just use container duration with a little fudge factor
    // (sigh about needing the fudge factor)

    double start_time = std::max((double)0, getStartTime());
    double end_time = start_time + getDuration() + 1;

    return getClipVolumeData(start_time, end_time, result, progress_func);
}

int VideoReader::readFrame(AVPacket *packet) {
    int ret;
    if (m_pending_packet_queue.isEmpty()) {
        ret = av_read_frame(m_av_format_context.get(), packet);
        if (ret < 0) {
            return ret;
        }
    } else {
        auto queued_packet = m_pending_packet_queue.removeFirst();
        av_packet_ref(packet, queued_packet.get());
        ret = 0;
    }

    if (packet->stream_index == m_stream_map.getVideoInputStreamIndex()) {
        //log(LOG_INFO, "read frame setting latest video pts to %li %f\n", packet->pts, convertVideoTsToSec(packet->pts));
        m_latest_video_pts = packet->pts;
        m_latest_video_duration_pts = packet->duration;
    }

    return ret;
}

bool VideoReader::safeSeek(int64_t pts, bool &is_eof) {
    is_eof = false;

    // for mp4s, if we want to seek to time X, libav will put us at the key_frame before X perfectly
    // hls does not support seeking at all, so we need to do this instead:
    // seek to 11s before the desired time (we insist we should be getting a key_frame every 10s or more often)
    // make a copy of all packets starting at the last video key_frame; if we get to another key_frame before the desired time, clear it
    // once we're at the desired time, write out the copied packets

    auto input_video_stream = m_stream_map.getVideoAvStream();

    // start 11s before the desired time
    int64_t seek_pts = pts - convertVideoSecToTs(11);

    int ret;

    if (seek_pts < input_video_stream->start_time) {
        seek_pts = input_video_stream->start_time;

        if (m_latest_video_pts < 0) {
            // we've never read anything, so we're already at the beginning
            return true;
        }
        log(LOG_INFO, "seeking to beginning of file generally skips to the first key frame, may not be intended\n");
    }

    m_pending_packet_queue.clear();

    if (m_video_av_codec_context) {
        avcodec_flush_buffers(m_video_av_codec_context.get());
    }

    //log(LOG_INFO, "safe seeking to pts %li %f\n", seek_pts, convertVideoTsToSec(seek_pts));

    ret = av_seek_frame(m_av_format_context.get(), m_stream_map.getVideoInputStreamIndex(), seek_pts, AVSEEK_FLAG_BACKWARD);
    // seeking that fails mostly return -1, which in libav is equivalent to EPERM, but it's not actually
    // a permission issue; it's just used as a generic error (such as trying to seek outside the video)
    // this was discovered by reading the av_seek_frame code.
    if (ret == -1 || ret == AVERROR_EOF) {
        is_eof = true;
        return false;
    }
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error seeking %i %s\n", ret, buf);
        return false;
    }

    // we setup m_pending_packet_queue to contain all packets starting with the last key frame
    // before the specified time

    double timestamp = convertVideoTsToSec(pts);

    bool is_done = false;

    //printf("read packets through pts starting with queue length %zu\n", m_pending_packet_queue.size());

    // put it in a smart pointer to get it properly freed in all cases
    auto packet = std::unique_ptr<AVPacket, AVPacketDeleter>(av_packet_alloc(), AVPacketDeleter());
    if (!packet) {
        log(LOG_ERROR, "Error allocating packet\n");
        return false;
    }

    while (!is_done) {
        int ret = av_read_frame(m_av_format_context.get(), packet.get());
        if (ret == AVERROR_EOF) {
            is_eof = true;
            return false;
        }
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            log(LOG_ERROR, "Error reading frame %i %s\n", ret, buf);
            return false;
        }

        // automatically unreference packet at end of loop
        AVPacketUnref packet_unref(packet.get());

        std::shared_ptr<StreamData> stream_data = m_stream_map.getStreamDataByInputStreamIndex(packet->stream_index);
        if (!stream_data) {
            // not a stream we care about
            continue;
        }
        double packet_sec = m_stream_map.convertTsToSec(stream_data, packet->pts);
        if (packet_sec - timestamp > MAX_LOOK_PAST_TIME_SEC) {
            log(LOG_ERROR, "cannot find key frame after looking %f seconds past the desired time, stream %i\n", MAX_LOOK_PAST_TIME_SEC, stream_data->input_stream_index);
            return false;
        }

        if (packet->stream_index == m_stream_map.getVideoInputStreamIndex()) {
            if (packet->flags == AV_PKT_FLAG_KEY) {
                if (packet->pts < pts) {
                    m_pending_packet_queue.clear();
                }
            }

            // we only stop when we are at a frame >= desired pts (usually this will be a bunch of packets
            // after the needed time)
            if (packet->pts + packet->duration >= pts) {
                //printf("read packets through pts done at %li %f\n", packet->pts, convertVideoTsToSec(packet->pts));
                is_done = true;
            }
        }

        if (!m_pending_packet_queue.add(packet.get())) {
            return false;
        }
    }

    return true;
}

bool VideoReader::initVideoCodecContext() {
    if (m_video_av_codec_context) {
        return true;
    }

    AVStream *stream = m_stream_map.getVideoAvStream();
    if (!stream) {
        log(LOG_ERROR, "no video av stream found\n");
        return false;
    }
    AVCodecParameters *in_codecpar = stream->codecpar;
    AVCodecID codec_id = in_codecpar->codec_id;
    const AVCodec *video_codec = avcodec_find_decoder(codec_id);
    if (video_codec == NULL) {
        log(LOG_ERROR, "Error getting reader video decoder for video codec id %u\n", codec_id);
        return false;
    }

    // use a smart pointer to get it properly freed in all cases
    m_video_av_codec_context = std::shared_ptr<AVCodecContext>(
        avcodec_alloc_context3(video_codec),
        AVCodecContextDeleter()
        );
    if (!m_video_av_codec_context) {
        log(LOG_ERROR, "Error allocating reader video codec context\n");
        return false;
    }

    int ret;

    ret = avcodec_parameters_to_context(m_video_av_codec_context.get(), in_codecpar);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error applying parameters to reader video context %i %s\n", ret, buf);

        m_video_av_codec_context = nullptr;
        return false;
    }

    ret = avcodec_open2(m_video_av_codec_context.get(), video_codec, NULL);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error opening reader video codec %i %s\n", ret, buf);

        m_video_av_codec_context = nullptr;
        return false;
    }

    return true;
}

bool VideoReader::initAudioCodecContext() {
    if (m_audio_av_codec_context) {
        return true;
    }

    // this should be the same as initVideoCodecContext except saving to m_audio_av_codec_context instead

    AVStream *stream = m_stream_map.getAudioAvStream();
    if (!stream) {
        log(LOG_ERROR, "no audio av stream found\n");
        return false;
    }
    AVCodecParameters *in_codecpar = stream->codecpar;
    AVCodecID codec_id = in_codecpar->codec_id;
    const AVCodec *audio_codec = avcodec_find_decoder(codec_id);
    if (audio_codec == NULL) {
        printf("Error getting reader audio decoder for audio codec id %u\n", codec_id);
        return false;
    }

    // use a smart pointer to get it properly freed in all cases
    m_audio_av_codec_context = std::shared_ptr<AVCodecContext>(
        avcodec_alloc_context3(audio_codec),
        AVCodecContextDeleter()
        );
    if (!m_audio_av_codec_context) {
        log(LOG_ERROR, "Error allocating reader audio codec context\n");
        return false;
    }

    int ret;

    ret = avcodec_parameters_to_context(m_audio_av_codec_context.get(), in_codecpar);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error applying parameters to reader audio context %i %s\n", ret, buf);

        m_audio_av_codec_context = nullptr;
        return false;
    }

    // wav files don't have a channel layout so we use the default
    // see https://stackoverflow.com/questions/20001363/avcodeccontextchannel-layout-0-for-wav-files
    if (!m_audio_av_codec_context->channel_layout) {
        m_audio_av_codec_context->channel_layout = av_get_default_channel_layout(m_audio_av_codec_context->channels);
    }
    m_audio_av_codec_context->request_channel_layout = m_audio_av_codec_context->channel_layout;

    ret = avcodec_open2(m_audio_av_codec_context.get(), audio_codec, NULL);
    if (ret < 0) {
        char buf[100];
        av_strerror(ret, buf, sizeof(buf));
        log(LOG_ERROR, "Error opening reader audio codec %i %s\n", ret, buf);

        m_audio_av_codec_context = nullptr;
        return false;
    }

    return true;
}

// This is super tricky too. Even though we have a sequence of packets to process that should get us through pts,
// it may or may not be the last image generated by avcodec_receive_frame due to B frames potentially existing
// near the end of the sequence and the fact that the codec can buffer images and not give them to us
// until a certain amount more is read.
bool VideoReader::readAndGetImage(int64_t pts, GetImageResult &get_image_result) {
    if (!initVideoCodecContext()) {
        return false;
    }

    double timestamp = convertVideoTsToSec(pts);

    // put it in a smart pointer to get it properly freed in all cases
    auto current_frame = std::unique_ptr<AVFrame, AVFrameDeleter>(av_frame_alloc(), AVFrameDeleter());
    if (!current_frame) {
        log(LOG_ERROR, "Error allocating current_frame\n");
        return false;
    }

    bool have_desired_frame = false;

    // build an accessory func here that we use in two loops below

    // returns true on no error, false on error
    auto receive_frames_func = [this, pts, &current_frame, &have_desired_frame](AVPacket *packet) -> bool {
        if (packet) {
            //printf("video packet key? %i time %li %f %li %f %li %f\n", (packet->flags & AV_PKT_FLAG_KEY) ? 1 : 0, packet->dts, convertVideoTsToSec(packet->dts), packet->pts, convertVideoTsToSec(packet->pts), packet->duration, convertVideoTsToSec(packet->duration));
            int ret = avcodec_send_packet(m_video_av_codec_context.get(), packet);
            if (ret < 0) {
                if (ret != AVERROR(EAGAIN)) {
                    char buf[100];
                    av_strerror(ret, buf, sizeof(buf));
                    log(LOG_ERROR, "Error sending packet to codec context %i %s\n", ret, buf);
                    return false;
                }
            }
        }

        while (!have_desired_frame) {
            av_frame_unref(current_frame.get());

            int ret = avcodec_receive_frame(m_video_av_codec_context.get(), current_frame.get());
            if (ret < 0) {
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                    break;
                }
                char buf[100];
                av_strerror(ret, buf, sizeof(buf));
                log(LOG_ERROR, "Error receiving frame %i %s\n", ret, buf);
                return false;
            }

            //printf("received frame %li %f %li %li %li\n", current_frame->pts, convertVideoTsToSec(current_frame->pts), current_frame->pts + current_frame->pkt_duration, current_frame->pkt_duration, pts);
            // pkt durations are 0 (meaning unknown) when reading ts files early on,
            // before the codec figures out the true frame rate.
            // After that (and in mp4 files) it is accurate
            int64_t packet_duration = current_frame->pkt_duration;
            if (packet_duration == 0) {
                // just guess 30fps;
                packet_duration = convertVideoSecToTs(1./30);
            }
            if (current_frame->pts + packet_duration > pts) {
                have_desired_frame = true;
            }
        }
        return true;
    };

    // read packets and decode as many images as possible until we find what we are looking for

    // put it in a smart pointer to get it properly freed in all cases
    auto packet = std::unique_ptr<AVPacket, AVPacketDeleter>(av_packet_alloc(), AVPacketDeleter());
    if (!packet) {
        log(LOG_ERROR, "Error allocating packet\n");
        return false;
    }

    bool is_done_reading = true;
    while (!have_desired_frame) {
        int ret = readFrame(packet.get());
        if (ret == AVERROR_EOF) {
            is_done_reading = true;
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

        std::shared_ptr<StreamData> stream_data = m_stream_map.getStreamDataByInputStreamIndex(packet->stream_index);
        if (!stream_data) {
            // not a stream we care about
            continue;
        }
        double packet_sec = m_stream_map.convertTsToSec(stream_data, packet->pts);
        if (packet_sec - timestamp > MAX_LOOK_PAST_TIME_SEC) {
            //log(LOG_INFO, "cannot find image even after looking %f seconds past the desired time %f, stream %i\n", MAX_LOOK_PAST_TIME_SEC, timestamp, stream_data->input_stream_index);
            // we return success but with no image; that seems like the right thing to do
            return true;
        }

        if (packet->stream_index != m_stream_map.getVideoInputStreamIndex()) {
            continue;
        }

        // pull out as many frames as possible, up until getting one covering pts
        if (!receive_frames_func(packet.get())) {
            return false;
        }
    }

    if (!have_desired_frame && is_done_reading) {
        // put codec context into draining mode to get out any queued images
        int ret = avcodec_send_packet(m_video_av_codec_context.get(), NULL);
        if (ret < 0) {
            char buf[100];
            av_strerror(ret, buf, sizeof(buf));
            log(LOG_ERROR, "Error sending null packet to codec context %i %s\n", ret, buf);
            return false;
        }

        avcodec_flush_buffers(m_video_av_codec_context.get());
        // pull out as many frames as possible, up until getting one covering pts
        if (!receive_frames_func(NULL)) {
            return false;
        }
    }

    // we may or may not have gotten a frame at the desired time here

    if (current_frame->pts == AV_NOPTS_VALUE) {
        get_image_result.is_eof = true;
        return false;
    }

    get_image_result.timestamp = convertVideoTsToSec(current_frame->pts);
    get_image_result.duration = convertVideoTsToSec(current_frame->pkt_duration);
    ImageInterface &image = get_image_result.image;

    if (!image.isInitialized()) {
        if (!image.init(current_frame->width, current_frame->height)) {
            log(LOG_ERROR, "failed to alloc image\n");
            return false;
        }
    }

    // put it in a smart pointer to get it properly freed in all cases
    auto rgb_frame = std::unique_ptr<AVFrame, AVFrameDeleter>(av_frame_alloc(), AVFrameDeleter());
    if (!rgb_frame) {
        log(LOG_ERROR, "failed to alloc rgb frame\n");
        return false;
    }

    av_image_alloc(rgb_frame->data, rgb_frame->linesize, image.getWidth(), image.getHeight(), AV_PIX_FMT_RGB24, av_cpu_max_align());
    auto buffer = std::unique_ptr<uint8_t, AVRawDeleter>(rgb_frame->data[0], AVRawDeleter());

    int scale_mode = SWS_BICUBIC;

    // should adjust the input pixel format if needed to avoid deprecated yuvj and instead do yuv and set color range
    // AV_PIX_FMT_YUVJ420P,  ///< planar YUV 4:2:0, 12bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV420P and setting color_range
    // AV_PIX_FMT_YUVJ422P,  ///< planar YUV 4:2:2, 16bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV422P and setting color_range
    // AV_PIX_FMT_YUVJ444P,  ///< planar YUV 4:4:4, 24bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV444P and setting color_range
    // https://stackoverflow.com/questions/23067722/swscaler-warning-deprecated-pixel-format-used
    // that would avoid this warning:
    // libav: [swscaler] deprecated pixel format used, make sure you did set range correctly

    // put it in a smart pointer to get it properly freed in all cases
    auto sws_context = std::unique_ptr<SwsContext, SwsContextDeleter>(
        sws_getContext(current_frame->width, current_frame->height, (AVPixelFormat)current_frame->format, image.getWidth(), image.getHeight(), AV_PIX_FMT_RGB24, scale_mode, NULL, NULL, NULL),
        SwsContextDeleter()
        );
    if (!sws_context) {
        log(LOG_ERROR, "failed to alloc sws context\n");
        return false;
    }

    // (possibly scale and) convert image to rgb
    sws_scale(sws_context.get(), current_frame->data, current_frame->linesize, 0, current_frame->height, rgb_frame->data, rgb_frame->linesize);

    // copy to the output Image object
    for (int i = 0; i < image.getHeight(); i++) {
        image.setRow(i, rgb_frame->data[0] + (i * rgb_frame->linesize[0]));
    }

    return true;
}
