/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>

#include <string>

#include "../utils.h"
#include "../video_reader.h"

#include "file_io_group.h"

void logProgress(int step, int total) {
    printf("progress %i/%i\n", step, total);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Need filename to read, filename to write, start_time, and end_time\n");
        return 1;
    }

    std::string source_pathname = argv[1];
    std::string dest_pathname = argv[2];

    double start_time = std::stod(argv[3]);
    double end_time = std::stod(argv[4]);

    Avalanche::setDefaultLogFunc();

    printf("lavf version %s\n", Avalanche::getAvFormatVersionString().c_str());

    FileIoGroup file_io_group;

    Avalanche::VideoReader video_reader;

    if (!video_reader.init(&file_io_group, source_pathname)) {
        printf("video reader init failed\n");
        return 1;
    }

    if (!video_reader.verifyHasVideoStream()) {
        printf("video has no video stream\n");
        return 1;
    }

    Avalanche::ExtractClipResult clip_result;
    if (!video_reader.extractClipReencode(dest_pathname, start_time, end_time, clip_result, logProgress)) {
        printf("failed to extract clip reencode\n");
        return 1;
    }
    printf("total video packets %i key frames %i start_time %f duration %f\n", clip_result.count_video_packets, clip_result.count_key_frames, clip_result.video_start_time, clip_result.video_duration);

    return 0;
}
