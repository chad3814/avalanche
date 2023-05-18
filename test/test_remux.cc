/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>

#include <string>

#include "../custom_io_group.h"
#include "../remux.h"
#include "../utils.h"

#include "file_io_group.h"

void logProgress(int step, int total) {
    printf("progress %i/%i\n", step, total);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Need filename to read and filename to write\n");
        return 1;
    }

    std::string source_pathname = argv[1];
    std::string dest_pathname = argv[2];

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

    Avalanche::ExtractClipResult clip_data;
    if (!video_reader.remux(dest_pathname, clip_data, logProgress)) {
        printf("failed to remux\n");
        return 1;
    }
    printf("total video packets %i key frames %i start_time %f duration %f\n", clip_data.count_video_packets, clip_data.count_key_frames, clip_data.video_start_time, clip_data.video_duration);

    return 0;
}
