/**
 * (c) Chad Walker, Chris Kirmse
 */

#include "../uv_mutex_lock.h"

#include "resource_io_group.h"

using namespace Avalanche;

struct OpenFileContext {
    OpenFileContext(ResourceIoGroup *resource_io_group) :
        resource_io_group(resource_io_group) {
    }
    ResourceIoGroup *resource_io_group = nullptr;
    bool is_done = false;
    bool success = false;
    int64_t file_size = 0;
};

typedef std::vector<std::pair<uint8_t *, int>> BufferVector;

struct ReadFileContext {
    ReadFileContext(ResourceIoGroup *resource_io_group) :
        resource_io_group(resource_io_group) {
    }
    ResourceIoGroup *resource_io_group = nullptr;
    bool is_done = false;
    bool success = false;
    BufferVector buffer_vector;
};

ResourceIoGroup::ResourceIoGroup(const Napi::Object &resource_io_obj):
    m_resource_io_obj_ref(Napi::Persistent(resource_io_obj)) {

    auto env = resource_io_obj.Env();

    Napi::Function open_file_func = resource_io_obj.Get("openFile").As<Napi::Function>();
    Napi::Function close_file_func = resource_io_obj.Get("closeFile").As<Napi::Function>();
    Napi::Function read_file_func = resource_io_obj.Get("readFile").As<Napi::Function>();

    // we unref all these functions immediately so they don't prevent the main js loop from exiting

    auto finalizer_open_file = [](const Napi::Env &) {};
    m_open_file_func = Napi::ThreadSafeFunction::New(env, open_file_func, "open_file_func", 0, 1, finalizer_open_file);
    m_open_file_func.Unref(env);

    auto finalizer_close_file = [](const Napi::Env &) {};
    m_close_file_func = Napi::ThreadSafeFunction::New(env, close_file_func, "close_file_func", 0, 1, finalizer_close_file);
    m_close_file_func.Unref(env);

    auto finalizer_read_file = [](const Napi::Env &) {};
    m_read_file_func = Napi::ThreadSafeFunction::New(env, read_file_func, "read_file_func", 0, 1, finalizer_read_file);
    m_read_file_func.Unref(env);

    int ret;

    ret = uv_mutex_init(&m_mutex);
    if (ret != 0) {
        log(LOG_ERROR, "UvMutexInitFailed %i", ret);
        return;
    }
    ret = uv_cond_init(&m_cond);
    if (ret != 0) {
        log(LOG_ERROR, "UvCondInitFailed %i", ret);
        return;
    }
}

ResourceIoGroup::~ResourceIoGroup() {
    //printf("ResourceIoGroup::~ResourceIoGroup\n");

    for (auto resource_io: m_resource_ios) {
        delete resource_io;
    }
    m_resource_ios.clear();

    uv_mutex_destroy(&m_mutex);
    uv_cond_destroy(&m_cond);

    m_resource_io_obj_ref.Unref();
}

void ResourceIoGroup::setStopProcessing() {
    //printf("ResourceIoGroup::setStopProcessing\n");

    m_allow_processing = false;
    uv_cond_signal(&m_cond);
}

Napi::Value ResourceIoGroup::wrappedOpenFileResolveHandler(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsNull() && !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Value val_resolve = info[0];

    auto open_file_context = (OpenFileContext *)info.Data();

    open_file_context->resource_io_group->lock([open_file_context, &val_resolve] {
        if (val_resolve.IsNull()) {
            open_file_context->success = false;
            open_file_context->file_size = 0;
        } else {
            open_file_context->success = true;
            open_file_context->file_size = val_resolve.As<Napi::Number>().Int64Value();
        }

        open_file_context->is_done = true;
    });
    uv_cond_signal(&open_file_context->resource_io_group->m_cond);

    return env.Null();
}

AVIOContext * ResourceIoGroup::open(const std::string &uri) {
    if (!m_allow_processing) {
        return NULL;
    }
    //printf("ResourceIoGroup::open %s\n", uri.c_str());

    napi_status status;

    status = m_open_file_func.Acquire();
    if (status != napi_ok) {
        printf("failed to acquire open %i\n", status);
        return NULL;
    }

    auto open_file_context = OpenFileContext(this);

    status = m_open_file_func.BlockingCall([this, uri, &open_file_context](const Napi::Env &env, const Napi::Function &js_func) {
        // this code is run in the main js thread
        Napi::HandleScope scope(env);

        Napi::Value val_uri = Napi::String::New(env, uri);
        Napi::Value result = js_func.Call(m_resource_io_obj_ref.Value(), {val_uri});

        // connect a callback to the promise resolve
        Napi::Promise promise = result.As<Napi::Promise>();
        Napi::Function then_func = promise.Get("then").As<Napi::Function>();
        auto data = (void *)&open_file_context;
        Napi::Function resolve_handler_func = Napi::Function::New(env, ResourceIoGroup::wrappedOpenFileResolveHandler, "openFileResolve", data);
        then_func.Call(promise, {resolve_handler_func});
    });

    if (status != napi_ok) {
        printf("failed to call js_func to open file\n");
        if (status == napi_closing) {
            return NULL;
        }
    }

    status = m_open_file_func.Release();
    if (status != napi_ok) {
        printf("failed to release open %i\n", status);
        return NULL;
    }

    lock([this, uri, &open_file_context]() {
        while (!open_file_context.is_done && m_allow_processing) {
            uv_cond_wait(&m_cond, &m_mutex);
        }
    });

    if (!m_allow_processing) {
        return NULL;
    }

    if (!open_file_context.success) {
        m_allow_processing = false;
        return NULL;
    }

    auto resource_io = new ResourceIo(this, uri, open_file_context.file_size);
    m_resource_ios.insert(resource_io);

    //printf("ResourceIoGroup::open %s returning\n", uri.c_str());

    return resource_io->getAvioContext();
}

void ResourceIoGroup::close(void *opaque) {
    //printf("ResourceIoGroup::close\n");

    if (!m_allow_processing) {
        return;
    }

    ResourceIo *resource_io = static_cast<ResourceIo *>(opaque);
    const std::string uri = resource_io->getUri();

    //printf("ResourceIoGroup::close of uri %s\n", uri.c_str());

    m_resource_ios.erase(resource_io);
    delete resource_io;

    lock([this, &uri]() {
        m_file_sizes.erase(uri);
    });

    // tell javascript to close the file

    napi_status status;

    status = m_close_file_func.Acquire();
    if (status != napi_ok) {
        printf("failed to acquire close %i\n", status);
        return;
    }

    bool is_done = false;

    //printf("acquired close file func, going to start call into js thread\n");
    status = m_close_file_func.BlockingCall([this, uri, &is_done](const Napi::Env &env, const Napi::Function &js_func) {
        // this code is run in the main js thread
        //printf("ResourceIoGroup::close js thread of uri %s\n", uri.c_str());
        Napi::HandleScope scope(env);

        Napi::Value val_uri = Napi::String::New(env, uri);

        js_func.Call(m_resource_io_obj_ref.Value(), {val_uri});

        lock([&is_done]() {
            is_done = true;
        });

        uv_cond_signal(&m_cond);
    });

    if (status != napi_ok) {
        printf("failed to call js_func to close file\n");
        if (status == napi_closing) {
            return;
        }
    }

    status = m_close_file_func.Release();
    if (status != napi_ok) {
        printf("failed to release close %i\n", status);
        return;
    }

    lock([this, &is_done]() {
        while (!is_done && m_allow_processing) {
            uv_cond_wait(&m_cond, &m_mutex);
        }
    });

    //printf("ResourceIoGroup::close %s returning\n", uri.c_str());
};

int ResourceIoGroup::interruptCallback() {
    if (!m_allow_processing) {
        printf("ResourceIoGroup::interruptCallback returning 1\n");
        return 1;
    }
    return 0;
}

Napi::Value ResourceIoGroup::wrappedReadFileResolveHandler(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        std::string err = "Wrong number of arguments " + info.Length();
        Napi::TypeError::New(env, err.c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsNull() && !info[0].IsArray()) {
        Napi::TypeError::New(env, "Wrong argument 0").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Value val_resolve = info[0];

    auto read_file_context = (ReadFileContext *)info.Data();

    read_file_context->resource_io_group->lock([read_file_context, &val_resolve] {
        if (val_resolve.IsNull()) {
            read_file_context->success = false;
        } else {
            read_file_context->success = true;
            auto buffer_arr(val_resolve.As<Napi::Array>());

            int len = 0;
            for (uint32_t i = 0; i < buffer_arr.Length(); i++) {
                Napi::Buffer<uint8_t> buffer = buffer_arr.Get(i).As<Napi::Buffer<uint8_t>>();
                read_file_context->buffer_vector.push_back(std::make_pair(buffer.Data(), buffer.Length()));
                len += buffer.Length();
            }
        }
        read_file_context->is_done = true;
    });
    uv_cond_signal(&read_file_context->resource_io_group->m_cond);

    return env.Null();
}

int ResourceIoGroup::read(const std::string &uri, int64_t read_offset, uint8_t *buf, int buf_size) {
    if (!m_allow_processing) {
        return -1;
    }
    //printf("ResourceIoGroup::read %s %li %i\n", uri.c_str(), read_offset, buf_size);

    napi_status status;

    status = m_read_file_func.Acquire();
    if (status != napi_ok) {
        printf("failed to acquire request data %i\n", status);
        return -1;
    }

    auto read_file_context = ReadFileContext(this);

    //printf("acquired read file func, going to start call into js thread\n");
    status = m_read_file_func.BlockingCall([this, uri, read_offset, buf_size, &read_file_context](const Napi::Env &env, const Napi::Function &js_func) {
        // this code is run in the main js thread
        Napi::HandleScope scope(env);

        Napi::Value val_uri = Napi::String::New(env, uri);
        Napi::Value val_read_offset = Napi::Number::New(env, read_offset);
        Napi::Value val_buf_size = Napi::Number::New(env, buf_size);

        Napi::Value result = js_func.Call(m_resource_io_obj_ref.Value(), {val_uri, val_read_offset, val_buf_size});

        // connect a callback to the promise resolve
        Napi::Promise promise = result.As<Napi::Promise>();
        Napi::Function then_func = promise.Get("then").As<Napi::Function>();
        auto data = (void *)&read_file_context;
        Napi::Function resolve_handler_func = Napi::Function::New(env, ResourceIoGroup::wrappedReadFileResolveHandler, "readFileResolve", data);
        then_func.Call(promise, {resolve_handler_func});
    });

    if (status != napi_ok) {
        printf("failed to call js_func to request data\n");
        if (status == napi_closing) {
            return -1;
        }
    }

    status = m_read_file_func.Release();
    if (status != napi_ok) {
        printf("failed to release request data %i\n", status);
        return -1;
    }

    lock([this, uri, &read_file_context]() {
        while (!read_file_context.is_done && m_allow_processing) {
            uv_cond_wait(&m_cond, &m_mutex);
        }
    });

    if (!m_allow_processing) {
        return -1;
    }

    if (!read_file_context.success) {
        m_allow_processing = false;
        return -1;
    }

    uint8_t *result_buf;
    int len_result_buf;

    int bytes_written = 0;
    for (auto buf_entry: read_file_context.buffer_vector) {
        std::tie(result_buf, len_result_buf) = buf_entry;
        int len_copy = std::min(buf_size - bytes_written, len_result_buf);
        memcpy(buf + bytes_written, result_buf, len_copy);
        bytes_written += len_copy;
    }

    //printf("ResourceIoGroup::read processed %i bytes from js\n", bytes_written);

    return bytes_written;
}

void ResourceIoGroup::lock(std::function<void()> func) {
    UvMutexLock lock(m_mutex);

    func();
}
