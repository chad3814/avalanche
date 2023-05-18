/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <stdio.h>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

#include "private/utils.h"

#include "utils.h"

using namespace Avalanche;

LogFuncType log_func;

std::string pending_str;
void defaultLogFunc(int level, bool is_libav, const char *s) {
    pending_str += s;
    if (pending_str.size() == 0 || pending_str[pending_str.size() - 1] != '\n') {
        return;
    }

    const char *level_str = "";
    const char *libav_str = "";
    if (is_libav) {
        libav_str = "libav: ";
    } else {
        if (level == LOG_ERROR) {
            level_str = "error ";
        }
    }
    printf("%s%s%s", libav_str, level_str, pending_str.c_str());
    pending_str.clear();
}

void avLogCallback(void *ptr, int level, const char *fmt, va_list valist)
{
    char line[1024];
    AVClass *avclass = ptr ? *(AVClass**)ptr : NULL;
    if (level > AV_LOG_INFO) {
        return;
    }

    line[0] = 0;

    if (avclass && strcmp(avclass->item_name(ptr), "NULL") != 0 ) {
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s] ", avclass->item_name(ptr));
    }
    vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, valist);

    int log_level;
    if (level <= AV_LOG_ERROR) {
        log_level = LOG_ERROR;
    } else if (level <= AV_LOG_INFO) {
        log_level = LOG_INFO;
    } else {
        log_level = LOG_DEBUG;
    }
    bool is_libav = true;
    log_func(log_level, is_libav, line);
}

void Avalanche::log(int level, const char *fmt, ...) {
    char line[1024];

    va_list valist;
    va_start(valist, fmt);
    vsnprintf(line, sizeof(line), fmt, valist);
    bool is_libav = false;
    if (log_func) {
        log_func(level, is_libav, line);
    } else {
        printf("%i %i %s", level, is_libav, line);
    }
    va_end(valist);
}

void Avalanche::setLogFunc(const LogFuncType &new_log_func) {
    log_func = new_log_func;
    av_log_set_callback(avLogCallback);

    // to get very detailed logging from libav
    //av_log_set_level(AV_LOG_DEBUG);
}

void Avalanche::setDefaultLogFunc() {
    log_func = defaultLogFunc;
    av_log_set_callback(avLogCallback);

    // to get very detailed logging from libav
    //av_log_set_level(AV_LOG_DEBUG);
}

std::string Avalanche::getAvFormatVersionString() {
    return std::to_string(LIBAVFORMAT_VERSION_MAJOR).append(".").append(std::to_string(LIBAVFORMAT_VERSION_MINOR)).append(".").append(std::to_string(LIBAVFORMAT_VERSION_MICRO));
}
