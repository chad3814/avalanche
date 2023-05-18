/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <set>

#include <napi.h>

#include "../video_reader.h"

#include "buffer_image.h"
#include "resource_io_group.h"

typedef std::unordered_set<BufferImage *> BufferImageSet;

class WrappedVideoReader : public Napi::ObjectWrap<WrappedVideoReader>
{
public:
    WrappedVideoReader(const Napi::CallbackInfo &info);
    ~WrappedVideoReader();

    virtual void Finalize(Napi::Env env) override;

    Napi::Value init(const Napi::CallbackInfo &info);
    Napi::Value destroy(const Napi::CallbackInfo &info);
    Napi::Value drain(const Napi::CallbackInfo &info);
    Napi::Value verifyHasVideoStream(const Napi::CallbackInfo &info);
    Napi::Value verifyHasAudioStream(const Napi::CallbackInfo &info);
    Napi::Value getImageAtTimestamp(const Napi::CallbackInfo &info);
    Napi::Value getMetadata(const Napi::CallbackInfo &info);
    Napi::Value extractClipReencode(const Napi::CallbackInfo &info);
    Napi::Value extractClipRemux(const Napi::CallbackInfo &info);
    Napi::Value remux(const Napi::CallbackInfo &info);
    Napi::Value getClipVolumeData(const Napi::CallbackInfo &info);
    Napi::Value getVolumeData(const Napi::CallbackInfo &info);

    static Napi::Function GetClass(Napi::Env env);

private:
    std::shared_ptr<ResourceIoGroup> m_resource_io_group;

    BufferImageSet m_pending_buffer_images;

    Avalanche::VideoReader m_video_reader;
};
