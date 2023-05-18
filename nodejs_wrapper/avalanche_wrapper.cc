/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <iostream>

#include "napi.h"

#include "../utils.h"

#include "promise_worker.h"
#include "resource_io_group.h"
#include "wrapped_stress_test_resource_io.h"
#include "wrapped_video_reader.h"

// there can be only one logger at a time, because libav's callbacks to us
// (to avalancheLogToJs) include no context whatsoever

Napi::ThreadSafeFunction ts_log_func;

using namespace Avalanche;

void avalancheLogToJs(int level, bool is_libav, const char *s) {
    napi_status status;

    if (!ts_log_func) {
        return;
    }

    status = ts_log_func.Acquire();
    if (status != napi_ok) {
        printf("failed to acquire %i\n", status);
        return;
    }

    char *copy_s = strndup(s, 1000);

    status = ts_log_func.BlockingCall(copy_s, [level, is_libav](const Napi::Env &env, const Napi::Function &js_func, const char *copy_s) {
        Napi::Value val_level;
        switch (level) {
        case LOG_INFO:
            val_level = Napi::String::New(env, "info");
            break;
        case LOG_ERROR:
            val_level = Napi::String::New(env, "error");
            break;
        case LOG_DEBUG:
            val_level = Napi::String::New(env, "debug");
            break;
        default:
            val_level = Napi::String::New(env, "unknown");
            break;
        }
        Napi::Value val_is_libav = Napi::Boolean::New(env, is_libav);
        Napi::Value val_s = Napi::String::New(env, copy_s);
        free((void *)copy_s);

        js_func.Call({val_level, val_is_libav, val_s});
    });

    if (status != napi_ok) {
        printf("failed to call js_func for logging\n");
        free(copy_s);
        if (status == napi_closing) {
            return;
        }
    }

    status = ts_log_func.Release();
    if (status != napi_ok) {
        printf("failed to release %i\n", status);
        return;
    }
}

Napi::Value wrappedSetLogFunc(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[0].IsFunction()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (ts_log_func) {
        ts_log_func.Release();
    }

    Napi::Function log_func = info[0].As<Napi::Function>();

    auto finalizer = [](const Napi::Env &) {};
    ts_log_func = Napi::ThreadSafeFunction::New(env, log_func, "avalanche_log", 0, 1, finalizer);

    // if something is crashing, it's handy to get logging to print out without having to go through javascript
    // by running this instead:
    // setDefaultLogFunc();
    setLogFunc(avalancheLogToJs);

    return env.Null();
}

Napi::Value destroy(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (ts_log_func) {
        ts_log_func.Release();
        ts_log_func = nullptr;
    }

    return env.Null();
}

Napi::Value wrappedGetAvFormatVersionString(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    return Napi::String::New(env, getAvFormatVersionString());
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "setLogFunc"), Napi::Function::New(env, wrappedSetLogFunc));
    exports.Set(Napi::String::New(env, "destroy"), Napi::Function::New(env, destroy));

    exports.Set(Napi::String::New(env, "stressTestResourceIo"), Napi::Function::New(env, wrappedStressTestResourceIo));

    exports.Set(Napi::String::New(env, "getAvFormatVersionString"), Napi::Function::New(env, wrappedGetAvFormatVersionString));

    exports.Set(Napi::String::New(env, "VideoReader"), WrappedVideoReader::GetClass(env));

    return exports;
}

NODE_API_MODULE(wrapper, Init)
