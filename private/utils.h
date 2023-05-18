/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <stdarg.h>

#include <string>

namespace Avalanche {

#define DEBUGGER (__asm__ volatile("int $0x03"))

void log(int level, const char *fmt, ...);

bool stringEndsWith(const std::string &s, const std::string &suffix);

}
