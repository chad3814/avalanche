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
    if (argc < 4) {
        printf("Need filename to read, start_time, and end_time\n");
        return 1;
    }

    std::string source_pathname = argv[1];

    double start_time = std::stod(argv[2]);
    double end_time = std::stod(argv[3]);

    Avalanche::setDefaultLogFunc();

    printf("lavf version %s\n", Avalanche::getAvFormatVersionString().c_str());

    FileIoGroup file_io_group;

    Avalanche::VideoReader video_reader;

    if (!video_reader.init(&file_io_group, source_pathname)) {
        printf("video reader init failed\n");
        return 1;
    }

    if (!video_reader.verifyHasAudioStream()) {
        printf("video has no audio stream\n");
        return 1;
    }

    Avalanche::GetVolumeDataResult get_volume_data_result;
    if (!video_reader.getClipVolumeData(start_time, end_time, get_volume_data_result, logProgress)) {
        printf("failed to get clip volume data\n");
        return 1;
    }
    printf("mean_volume %f dB max_volume %f dB\n", get_volume_data_result.mean_volume, get_volume_data_result.max_volume);

    return 0;
}
