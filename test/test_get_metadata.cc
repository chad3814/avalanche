/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>

#include <string>

#include "../utils.h"
#include "../video_reader.h"

#include "file_io_group.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Need filename to read\n");
        return 1;
    }

    std::string source_pathname = argv[1];

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

    Avalanche::GetMetadataResult get_metadata_result;
    if (!video_reader.getMetadata(get_metadata_result)) {
        printf("failed to get metadata\n");
        return 1;
    }

    printf("video_encoding_name: %s\n", get_metadata_result.video_encoding_name.c_str());
    printf("audio_encoding_name: %s\n", get_metadata_result.audio_encoding_name.c_str());

    printf("container_start_time: %f\n", get_metadata_result.container_start_time);
    printf("container_duration: %f\n", get_metadata_result.container_duration);

    printf("video_start_time: %f\n", get_metadata_result.video_start_time);
    printf("video_duration: %f\n", get_metadata_result.video_duration);
    printf("video_width: %i\n", get_metadata_result.video_width);
    printf("video_height: %i\n", get_metadata_result.video_height);
    printf("frame_rate: %f\n", get_metadata_result.frame_rate);

    return 0;
}
