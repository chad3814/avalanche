/**
 * (c) Chad Walker, Chris Kirmse
 */

#include "../uv_mutex_lock.h"

#include "buffer_image.h"
#include "promise_worker.h"
#include "wrapped_video_reader.h"

using namespace Avalanche;

WrappedVideoReader::WrappedVideoReader(const Napi::CallbackInfo &info) : ObjectWrap(info) {
    Napi::Env env = info.Env();

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return;
    }

}

WrappedVideoReader::~WrappedVideoReader() {
    //printf("WrappedVideoReader::~WrappedVideoReader\n");

    if (m_resource_io_group) {
        // need to tell it to not do anything, because when we destroy the video reader it will
        // call close() on any open files, and resource io close() calls javascript, and if
        // javascript is shutting down then no queued javascript functions will be called
        // and the close() would hang
        m_resource_io_group->setStopProcessing();
    }

    for (auto buffer_image: m_pending_buffer_images) {
        // BufferImage has the same issue as resource_io_group--it calls back to javascript,
        // so we need to trigger all pending callbacks to stop now
        buffer_image->drain();
    }

    m_video_reader.destroy();

    if (m_resource_io_group) {
        m_resource_io_group = nullptr;
    }

}

void WrappedVideoReader::Finalize(Napi::Env) {
    //printf("WrappedVideoReader::Finalize\n");
}

class InitWorker : public PromiseWorker {
public:
    InitWorker(
        const Napi::Promise::Deferred &deferred,
        std::shared_ptr<ResourceIoGroup> resource_io_group,
        VideoReader &video_reader,
        const std::string &uri) :
        PromiseWorker(deferred),
        m_resource_io_group(resource_io_group),
        m_video_reader(video_reader),
        m_uri(uri) {
    }

    virtual ~InitWorker() {
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        if (!m_video_reader.init(m_resource_io_group.get(), m_uri)) {
            m_success = false;
            return;
        }

        m_success = true;
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto value = Napi::Boolean::New(deferred.Env(), m_success);
        deferred.Resolve(value);
    }

private:
    std::shared_ptr<ResourceIoGroup> m_resource_io_group;
    VideoReader &m_video_reader;
    std::string m_uri;

    bool m_success = false;
};

Napi::Value WrappedVideoReader::init(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString() && !info[0].IsObject()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string source_uri;
    auto resource_io_obj = info[0].As<Napi::Object>();
    if (info[0].IsObject()) {
        m_resource_io_group = std::make_shared<ResourceIoGroup>(resource_io_obj);
        Napi::Function get_primary_uri_func = resource_io_obj.Get("getPrimaryUri").As<Napi::Function>();
        auto val_uri = get_primary_uri_func.Call(resource_io_obj, {});
        source_uri = val_uri.As<Napi::String>();
    } else {
        source_uri = info[0].As<Napi::String>();
    }

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    InitWorker *worker = new InitWorker(deferred, m_resource_io_group, m_video_reader, source_uri);
    worker->Queue();

    return deferred.Promise();
}

Napi::Value WrappedVideoReader::destroy(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    //printf("WrappedVideoReader::destroy\n");

    if (m_resource_io_group) {
        // need to tell it to not do anything, because when we destroy the video reader it will
        // call close() on any open files, and resource io close() calls javascript, and if
        // javascript is shutting down then no queued javascript functions will be called
        // and the close() would hang
        m_resource_io_group->setStopProcessing();
    }

    for (auto buffer_image: m_pending_buffer_images) {
        // BufferImage has the same issue as resource_io_group--it calls back to javascript,
        // so we need to trigger all pending callbacks to stop now
        buffer_image->drain();
    }

    m_video_reader.destroy();

    if (m_resource_io_group) {
        m_resource_io_group = nullptr;
    }

    //printf("WrappedVideoReader::destroy returning\n");

    return env.Null();
}

Napi::Value WrappedVideoReader::drain(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    //printf("WrappedVideoReader::drain\n");

    if (m_resource_io_group) {
        // need to tell it to not do anything, because when we destroy the video reader it will
        // call close() on any open files, and resource io close() calls javascript, and if
        // javascript is shutting down then no queued javascript functions will be called
        // and the close() would hang
        m_resource_io_group->setStopProcessing();
    }

    for (auto buffer_image: m_pending_buffer_images) {
        // BufferImage has the same issue as resource_io_group--it calls back to javascript,
        // so we need to trigger all pending callbacks to stop now
        buffer_image->drain();
    }

    //printf("WrappedVideoReader::drain returning\n");

    return env.Null();
}

Napi::Value WrappedVideoReader::verifyHasVideoStream(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    bool has_video_stream = m_video_reader.verifyHasVideoStream();
    auto value = Napi::Boolean::New(env, has_video_stream);

    return value;
}

Napi::Value WrappedVideoReader::verifyHasAudioStream(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    bool has_audio_stream = m_video_reader.verifyHasAudioStream();
    auto value = Napi::Boolean::New(env, has_audio_stream);

    return value;
}

class GetImageAtTimestampWorker : public PromiseWorker {
public:
    GetImageAtTimestampWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader,
        BufferImageSet &pending_buffer_images,
        double timestamp) :
        PromiseWorker(deferred),
        m_video_reader(video_reader),
        m_pending_buffer_images(pending_buffer_images),
        m_timestamp(timestamp),
        m_image(deferred.Env()) {
        m_pending_buffer_images.insert(&m_image);
    }

    virtual ~GetImageAtTimestampWorker() {
        m_pending_buffer_images.erase(&m_image);
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        if (!m_video_reader.getImageAtTimestamp(m_timestamp, m_get_image_result)) {
            if (m_get_image_result.is_eof) {
                SetError("Eof");
            } else {
                SetError("GetImageAtTimestampFailure");
            }
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        if (!m_image.isInitialized()) {
            // no error, but could not find an image there; this happens with bad videos
            Napi::Object result = Napi::Object::New(env);
            result.Set("net_image_buffer", env.Null());
            deferred.Resolve(result);
            return;
        }

        Napi::Reference<Napi::Buffer<uint8_t>> &buffer_ref = m_image.getBufferRef();
        auto net_image_buffer = buffer_ref.Value();

        Napi::Object result = Napi::Object::New(env);
        result.Set("net_image_buffer", net_image_buffer);
        result.Set("timestamp", Napi::Number::New(env, m_get_image_result.timestamp));
        result.Set("duration", Napi::Number::New(env, m_get_image_result.duration));

        buffer_ref.Unref();

        deferred.Resolve(result);
    }

private:
    VideoReader &m_video_reader;
    BufferImageSet &m_pending_buffer_images;
    double m_timestamp;

    BufferImage m_image;
    GetImageResult m_get_image_result = {false, m_image, 0, 0};
};

Napi::Value WrappedVideoReader::getImageAtTimestamp(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 1").ThrowAsJavaScriptException();
        return env.Null();
    }

    double timestamp(info[0].As<Napi::Number>().DoubleValue());

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    GetImageAtTimestampWorker *worker = new GetImageAtTimestampWorker(deferred, m_video_reader, m_pending_buffer_images, timestamp);
    worker->Queue();

    return deferred.Promise();
}

class GetMetadataWorker : public PromiseWorker {
public:
    GetMetadataWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader
        ) :
        PromiseWorker(deferred),
        m_video_reader(video_reader) {
    }

    virtual ~GetMetadataWorker() {
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        if (!m_video_reader.getMetadata(m_get_metadata_result)) {
            SetError("GetMetadataFailure");
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();
        Napi::Object metadata = Napi::Object::New(env);
        metadata.Set("video_encoding_name", Napi::String::New(env, m_get_metadata_result.video_encoding_name));
        metadata.Set("audio_encoding_name", Napi::String::New(env, m_get_metadata_result.audio_encoding_name));

        metadata.Set("container_start_time", Napi::Number::New(env, m_get_metadata_result.container_start_time));
        metadata.Set("container_duration", Napi::Number::New(env, m_get_metadata_result.container_duration));

        metadata.Set("video_start_time", Napi::Number::New(env, m_get_metadata_result.video_start_time));
        metadata.Set("video_duration", Napi::Number::New(env, m_get_metadata_result.video_duration));
        metadata.Set("video_width", Napi::Number::New(env, m_get_metadata_result.video_width));
        metadata.Set("video_height", Napi::Number::New(env, m_get_metadata_result.video_height));
        metadata.Set("frame_rate", Napi::Number::New(env, m_get_metadata_result.frame_rate));

        deferred.Resolve(metadata);
    }

private:
    VideoReader &m_video_reader;

    GetMetadataResult m_get_metadata_result;
};

Napi::Value WrappedVideoReader::getMetadata(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    GetMetadataWorker *worker = new GetMetadataWorker(deferred, m_video_reader);
    worker->Queue();

    return deferred.Promise();
}

class ExtractClipReencodeWorker : public PromiseWorker {
public:
    ExtractClipReencodeWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader,
        const std::string &dest_uri,
        double start_time,
        double end_time,
        const Napi::Function &progress_func
        ) :
        PromiseWorker(deferred),
        m_video_reader(video_reader),
        m_dest_uri(dest_uri),
        m_start_time(start_time),
        m_end_time(end_time) {

        auto finalizer = [](const Napi::Env &) {};
        m_progress_func = Napi::ThreadSafeFunction::New(deferred.Env(), progress_func, "progress_log", 0, 1, finalizer);
    }

    virtual ~ExtractClipReencodeWorker() {
        m_progress_func.Release();
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        // this function will also be executed on the worker thread, called back from remux
        auto progress_func = [this] (int step, int total) {
            m_progress_func.Acquire();
            napi_status status = m_progress_func.BlockingCall((void *)NULL, [step, total](const Napi::Env &env, const Napi::Function &js_func, void *) {
                // this code is run in the main js thread
                Napi::Value val_step = Napi::Number::New(env, step);
                Napi::Value val_total = Napi::Number::New(env, total);

                js_func.Call({val_step, val_total});
            });
            if (status != napi_ok) {
                printf("failed to call js_func for progress\n");
            }

            m_progress_func.Release();
        };

        if (!m_video_reader.extractClipReencode(m_dest_uri, m_start_time, m_end_time, m_extract_clip_result, progress_func)) {
            SetError("ExtractClipReencodeFailure");
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        Napi::Object result = Napi::Object::New(env);
        result.Set("video_start_time", Napi::Number::New(env, m_extract_clip_result.video_start_time));
        result.Set("video_duration", Napi::Number::New(env, m_extract_clip_result.video_duration));

        result.Set("count_video_packets", Napi::Number::New(env, m_extract_clip_result.count_video_packets));
        result.Set("count_key_frames", Napi::Number::New(env, m_extract_clip_result.count_key_frames));

        deferred.Resolve(result);
    }

private:
    VideoReader &m_video_reader;
    std::string m_dest_uri;
    double m_start_time;
    double m_end_time;
    Napi::ThreadSafeFunction m_progress_func;

    ExtractClipResult m_extract_clip_result;
};

Napi::Value WrappedVideoReader::extractClipReencode(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 4) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 1").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[2].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 2").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[3].IsFunction()) {
        Napi::TypeError::New(env, "Wrong argument 3").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string dest_uri(info[0].As<Napi::String>());
    double start_time(info[1].As<Napi::Number>().DoubleValue());
    double end_time(info[2].As<Napi::Number>().DoubleValue());
    Napi::Function progress_func = info[3].As<Napi::Function>();

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    ExtractClipReencodeWorker *worker = new ExtractClipReencodeWorker(deferred, m_video_reader, dest_uri, start_time, end_time, progress_func);
    worker->Queue();

    return deferred.Promise();
}

class ExtractClipRemuxWorker : public PromiseWorker {
public:
    ExtractClipRemuxWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader,
        const std::string &dest_uri,
        double start_time,
        double end_time,
        const Napi::Function &progress_func
        ) :
        PromiseWorker(deferred),
        m_video_reader(video_reader),
        m_dest_uri(dest_uri),
        m_start_time(start_time),
        m_end_time(end_time) {

        auto finalizer = [](const Napi::Env &) {};
        m_progress_func = Napi::ThreadSafeFunction::New(deferred.Env(), progress_func, "progress_log", 0, 1, finalizer);
    }

    virtual ~ExtractClipRemuxWorker() {
        m_progress_func.Release();
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        // this function will also be executed on the worker thread, called back from remux
        auto progress_func = [this] (int step, int total) {
            m_progress_func.Acquire();
            napi_status status = m_progress_func.BlockingCall((void *)NULL, [step, total](const Napi::Env &env, const Napi::Function &js_func, void *) {
                // this code is run in the main js thread
                Napi::Value val_step = Napi::Number::New(env, step);
                Napi::Value val_total = Napi::Number::New(env, total);

                js_func.Call({val_step, val_total});
            });
            if (status != napi_ok) {
                printf("failed to call js_func for progress\n");
            }

            m_progress_func.Release();
        };

        if (!m_video_reader.extractClipRemux(m_dest_uri, m_start_time, m_end_time, m_extract_clip_result, progress_func)) {
            SetError("ExtractClipRemuxFailure");
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        Napi::Object result = Napi::Object::New(env);
        result.Set("video_start_time", Napi::Number::New(env, m_extract_clip_result.video_start_time));
        result.Set("video_duration", Napi::Number::New(env, m_extract_clip_result.video_duration));

        result.Set("count_video_packets", Napi::Number::New(env, m_extract_clip_result.count_video_packets));
        result.Set("count_key_frames", Napi::Number::New(env, m_extract_clip_result.count_key_frames));

        deferred.Resolve(result);
    }

private:
    VideoReader &m_video_reader;
    std::string m_dest_uri;
    double m_start_time;
    double m_end_time;
    Napi::ThreadSafeFunction m_progress_func;

    ExtractClipResult m_extract_clip_result;
};

Napi::Value WrappedVideoReader::extractClipRemux(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 4) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 1").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[2].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 2").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[3].IsFunction()) {
        Napi::TypeError::New(env, "Wrong argument 3").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string dest_uri(info[0].As<Napi::String>());
    double start_time(info[1].As<Napi::Number>().DoubleValue());
    double end_time(info[2].As<Napi::Number>().DoubleValue());
    Napi::Function progress_func = info[3].As<Napi::Function>();

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    ExtractClipRemuxWorker *worker = new ExtractClipRemuxWorker(deferred, m_video_reader, dest_uri, start_time, end_time, progress_func);
    worker->Queue();

    return deferred.Promise();
}

class RemuxWorker : public PromiseWorker {
public:
    RemuxWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader,
        const std::string &dest_uri,
        const Napi::Function &progress_func
        ) :
        PromiseWorker(deferred),
        m_video_reader(video_reader),
        m_dest_uri(dest_uri) {

        auto finalizer = [](const Napi::Env &) {};
        m_progress_func = Napi::ThreadSafeFunction::New(deferred.Env(), progress_func, "progress_log", 0, 1, finalizer);
    }

    virtual ~RemuxWorker() {
        m_progress_func.Release();
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        // this function will also be executed on the worker thread, called back from remux
        auto progress_func = [this] (int step, int total) {
            m_progress_func.Acquire();
            napi_status status = m_progress_func.BlockingCall((void *)NULL, [step, total](const Napi::Env &env, const Napi::Function &js_func, void *) {
                // this code is run in the main js thread
                Napi::Value val_step = Napi::Number::New(env, step);
                Napi::Value val_total = Napi::Number::New(env, total);

                js_func.Call({val_step, val_total});
            });
            if (status != napi_ok) {
                printf("failed to call js_func for progress\n");
            }

            m_progress_func.Release();
        };

        if (!m_video_reader.remux(m_dest_uri, m_extract_clip_result, progress_func)) {
            SetError("RemuxFailure");
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        Napi::Object result = Napi::Object::New(env);
        result.Set("video_start_time", Napi::Number::New(env, m_extract_clip_result.video_start_time));
        result.Set("video_duration", Napi::Number::New(env, m_extract_clip_result.video_duration));

        result.Set("count_video_packets", Napi::Number::New(env, m_extract_clip_result.count_video_packets));
        result.Set("count_key_frames", Napi::Number::New(env, m_extract_clip_result.count_key_frames));

        deferred.Resolve(result);
    }

private:
    VideoReader &m_video_reader;
    std::string m_dest_uri;
    Napi::ThreadSafeFunction m_progress_func;

    ExtractClipResult m_extract_clip_result;
};

Napi::Value WrappedVideoReader::remux(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 2) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[1].IsFunction()) {
        Napi::TypeError::New(env, "Wrong argument 1").ThrowAsJavaScriptException();
        return env.Null();
    }


    std::string dest_uri(info[0].As<Napi::String>());
    Napi::Function progress_func = info[1].As<Napi::Function>();

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    RemuxWorker *worker = new RemuxWorker(deferred, m_video_reader, dest_uri, progress_func);
    worker->Queue();

    return deferred.Promise();
}

class GetClipVolumeDataWorker : public PromiseWorker {
public:
    GetClipVolumeDataWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader,
        double start_time,
        double end_time,
        const Napi::Function &progress_func
        ) :
        PromiseWorker(deferred),
        m_video_reader(video_reader),
        m_start_time(start_time),
        m_end_time(end_time) {

        auto finalizer = [](const Napi::Env &) {};
        m_progress_func = Napi::ThreadSafeFunction::New(deferred.Env(), progress_func, "progress_log", 0, 1, finalizer);
    }

    virtual ~GetClipVolumeDataWorker() {
        m_progress_func.Release();
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        // this function will also be executed on the worker thread, called back from remux
        auto progress_func = [this] (int step, int total) {
            m_progress_func.Acquire();
            napi_status status = m_progress_func.BlockingCall((void *)NULL, [step, total](const Napi::Env &env, const Napi::Function &js_func, void *) {
                // this code is run in the main js thread
                Napi::Value val_step = Napi::Number::New(env, step);
                Napi::Value val_total = Napi::Number::New(env, total);

                js_func.Call({val_step, val_total});
            });
            if (status != napi_ok) {
                printf("failed to call js_func for progress\n");
            }

            m_progress_func.Release();
        };

        if (!m_video_reader.getClipVolumeData(m_start_time, m_end_time, m_get_clip_volume_data_result, progress_func)) {
            SetError("GetClipVolumeDataFailure");
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        Napi::Object result = Napi::Object::New(env);
        result.Set("mean_volume", Napi::Number::New(env, m_get_clip_volume_data_result.mean_volume));
        result.Set("max_volume", Napi::Number::New(env, m_get_clip_volume_data_result.max_volume));

        deferred.Resolve(result);
    }

private:
    VideoReader &m_video_reader;
    double m_start_time;
    double m_end_time;
    Napi::ThreadSafeFunction m_progress_func;

    GetVolumeDataResult m_get_clip_volume_data_result;
};

Napi::Value WrappedVideoReader::getClipVolumeData(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 3) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 1").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[2].IsFunction()) {
        Napi::TypeError::New(env, "Wrong argument 2").ThrowAsJavaScriptException();
        return env.Null();
    }

    double start_time(info[0].As<Napi::Number>().DoubleValue());
    double end_time(info[1].As<Napi::Number>().DoubleValue());
    Napi::Function progress_func = info[2].As<Napi::Function>();

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    GetClipVolumeDataWorker *worker = new GetClipVolumeDataWorker(deferred, m_video_reader, start_time, end_time, progress_func);
    worker->Queue();

    return deferred.Promise();
}

class GetVolumeDataWorker : public PromiseWorker {
public:
    GetVolumeDataWorker(
        const Napi::Promise::Deferred &deferred,
        VideoReader &video_reader,
        const Napi::Function &progress_func
        ) :
        PromiseWorker(deferred),
        m_video_reader(video_reader) {
        auto finalizer = [](const Napi::Env &) {};
        m_progress_func = Napi::ThreadSafeFunction::New(deferred.Env(), progress_func, "progress_log", 0, 1, finalizer);
    }

    virtual ~GetVolumeDataWorker() {
        m_progress_func.Release();
    }

    // This code will be executed on the worker thread; not allowed to call any napi
    void Execute() override {
        // this function will also be executed on the worker thread, called back from remux
        auto progress_func = [this] (int step, int total) {
            m_progress_func.Acquire();
            napi_status status = m_progress_func.BlockingCall((void *)NULL, [step, total](const Napi::Env &env, const Napi::Function &js_func, void *) {
                // this code is run in the main js thread
                Napi::Value val_step = Napi::Number::New(env, step);
                Napi::Value val_total = Napi::Number::New(env, total);

                js_func.Call({val_step, val_total});
            });
            if (status != napi_ok) {
                printf("failed to call js_func for progress\n");
            }

            m_progress_func.Release();
        };

        if (!m_video_reader.getVolumeData(m_get_clip_volume_data_result, progress_func)) {
            SetError("GetVolumeDataFailure");
            return;
        }
    }

    void Resolve(Napi::Promise::Deferred const &deferred) override {
        auto env = deferred.Env();

        Napi::Object result = Napi::Object::New(env);
        result.Set("mean_volume", Napi::Number::New(env, m_get_clip_volume_data_result.mean_volume));
        result.Set("max_volume", Napi::Number::New(env, m_get_clip_volume_data_result.max_volume));

        deferred.Resolve(result);
    }

private:
    VideoReader &m_video_reader;
    Napi::ThreadSafeFunction m_progress_func;

    GetVolumeDataResult m_get_clip_volume_data_result;
};

Napi::Value WrappedVideoReader::getVolumeData(const Napi::CallbackInfo &info) {
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

    Napi::Function progress_func = info[0].As<Napi::Function>();

    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    GetVolumeDataWorker *worker = new GetVolumeDataWorker(deferred, m_video_reader, progress_func);
    worker->Queue();

    return deferred.Promise();
}

Napi::Function WrappedVideoReader::GetClass(Napi::Env env) {
    return DefineClass(env, "VideoReader", {
        WrappedVideoReader::InstanceMethod("init", &WrappedVideoReader::init),
        WrappedVideoReader::InstanceMethod("destroy", &WrappedVideoReader::destroy),
        WrappedVideoReader::InstanceMethod("drain", &WrappedVideoReader::drain),
        WrappedVideoReader::InstanceMethod("verifyHasVideoStream", &WrappedVideoReader::verifyHasVideoStream),
        WrappedVideoReader::InstanceMethod("verifyHasAudioStream", &WrappedVideoReader::verifyHasAudioStream),
        WrappedVideoReader::InstanceMethod("getImageAtTimestamp", &WrappedVideoReader::getImageAtTimestamp),
        WrappedVideoReader::InstanceMethod("getMetadata", &WrappedVideoReader::getMetadata),
        WrappedVideoReader::InstanceMethod("extractClipReencode", &WrappedVideoReader::extractClipReencode),
        WrappedVideoReader::InstanceMethod("extractClipRemux", &WrappedVideoReader::extractClipRemux),
        WrappedVideoReader::InstanceMethod("remux", &WrappedVideoReader::remux),
        WrappedVideoReader::InstanceMethod("getClipVolumeData", &WrappedVideoReader::getClipVolumeData),
        WrappedVideoReader::InstanceMethod("getVolumeData", &WrappedVideoReader::getVolumeData),
    });
}
