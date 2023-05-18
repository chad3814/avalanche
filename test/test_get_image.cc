/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>

#include <string>

#include "../image.h"
#include "../utils.h"
#include "../video_reader.h"

#include "file_io_group.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Need filename to read, filename to write, timestamp\n");
        return 1;
    }

    std::string source_pathname = argv[1];
    std::string dest_pathname = argv[2];

    double timestamp = std::stod(argv[3]);

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

    Avalanche::Image image;
    Avalanche::GetImageResult get_image_result = {false, image, 0, 0};

    if (!video_reader.getImageAtTimestamp(timestamp, get_image_result)) {
        if (get_image_result.is_eof) {
            printf("cannot get image as it is past eof\n");
        } else {
            printf("failed to get image\n");
        }
        return 1;
    }

    if (!get_image_result.image.savePpm(dest_pathname)) {
        printf("save failed\n");
        return 1;
    }
    printf("done\n");

    return 0;
}
