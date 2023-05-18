/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <iostream>

#include "promise_worker.h"
#include "resource_io_group.h"
#include "wrapped_stress_test_resource_io.h"

class StressTestResourceIoWorker : public PromiseWorker {
public:
    StressTestResourceIoWorker(const Napi::Promise::Deferred &deferred, std::shared_ptr<ResourceIoGroup> resource_io_group, const std::string &uri) :
        PromiseWorker(deferred),
        m_resource_io_group(resource_io_group),
        m_uri(uri) {
    }

    virtual ~StressTestResourceIoWorker() {
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        uint8_t read_buf[500];
        for (int i = 0; i < 5000000; i++) {
            if ((i % 10000) == 0) {
                printf("simulation %i\n", i);
            }
            char fake_m3u8[500];
            snprintf(fake_m3u8, sizeof(fake_m3u8), "/home/ckirmse/junk/new/playlist.m3u8");
            AVIOContext *input_format_context = m_resource_io_group->open(fake_m3u8);
            if (!input_format_context) {
                printf("error opening %s\n", fake_m3u8);
                break;
            }
            char fake_ts[500];
            snprintf(fake_ts, sizeof(fake_ts), "/home/ckirmse/junk/new/segment%07i.ts", i % 1942);
            AVIOContext *input_format_context2 = m_resource_io_group->open(fake_ts);
            if (!input_format_context2) {
                printf("error opening second %s\n", fake_ts);
                m_resource_io_group->close(input_format_context->opaque);
                break;
            }

            m_resource_io_group->read(fake_m3u8, 0, read_buf, sizeof(read_buf));
            m_resource_io_group->read(fake_m3u8, 0, read_buf, sizeof(read_buf));
            m_resource_io_group->read(fake_ts, 0, read_buf, sizeof(read_buf));
            m_resource_io_group->read(fake_ts, 0, read_buf, sizeof(read_buf));
            m_resource_io_group->close(input_format_context2->opaque);
            m_resource_io_group->close(input_format_context->opaque);
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        deferred.Resolve(env.Null());
    }

private:
    std::shared_ptr<ResourceIoGroup> m_resource_io_group;
    std::string m_uri;
};

Napi::Value wrappedStressTestResourceIo(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[0].IsObject()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::shared_ptr<ResourceIoGroup> resource_io_group;

    std::string source_uri;
    auto resource_io_obj = info[0].As<Napi::Object>();
    resource_io_group = std::make_shared<ResourceIoGroup>(resource_io_obj);
    Napi::Function get_primary_uri_func = resource_io_obj.Get("getPrimaryUri").As<Napi::Function>();
    auto val_uri = get_primary_uri_func.Call(resource_io_obj, {});
    source_uri = val_uri.As<Napi::String>();

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    StressTestResourceIoWorker *worker = new StressTestResourceIoWorker(deferred, resource_io_group, source_uri);
    worker->Queue();

    return deferred.Promise();
}
