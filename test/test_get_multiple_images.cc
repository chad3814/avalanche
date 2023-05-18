/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>

#include <string>

#include "../image.h"
#include "../video_reader.h"
#include "../utils.h"

#include "file_io_group.h"

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Need filename to read, filename template to write, start_time, end_time, gap_time\n");
        return 1;
    }

    std::string source_pathname = argv[1];
    std::string dest_pathname = argv[2];

    double start_time = std::stod(argv[3]);
    double end_time = std::stod(argv[4]);
    double gap_time = std::stod(argv[5]);
    Avalanche::setDefaultLogFunc();

    FileIoGroup file_io_group;

    Avalanche::VideoReader video_reader;

    if (!video_reader.init(&file_io_group, source_pathname)) {
        return 1;
    }

    if (!video_reader.verifyHasVideoStream()) {
        return 1;
    }

    double timestamp = start_time;
    while (timestamp < end_time) {
        Avalanche::Image image;

        Avalanche::GetImageResult get_image_result{false, image, 0, 0};

        if (!video_reader.getImageAtTimestamp(timestamp, get_image_result)) {
            if (get_image_result.is_eof) {
                printf("cannot get image at %f as it is past eof\n", timestamp);
            } else {
                printf("error getting image at timestamp %f\n", timestamp);
            }
            return 1;
        }
        printf("asked for image at timestamp %f actually got %f duration %f\n", timestamp, get_image_result.timestamp, get_image_result.duration);

        char filename[1024];
        snprintf(filename, sizeof(filename), dest_pathname.c_str(), get_image_result.timestamp);
        image.savePpm(filename);

        timestamp = get_image_result.timestamp + gap_time;
    }

    return 0;
}
