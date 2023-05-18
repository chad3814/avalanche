/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <string>

namespace Avalanche {

enum {
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_ERROR = 3,
};

typedef void (*LogFuncType)(int level, bool is_libav, const char *s);

void setLogFunc(const LogFuncType &new_log_func);
void setDefaultLogFunc();
std::string getAvFormatVersionString();

}
