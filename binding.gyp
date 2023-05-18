{
  "targets": [
    {
      "target_name": "avalanche",
      "sources": [
        "nodejs_wrapper/avalanche_wrapper.cc",
        "nodejs_wrapper/buffer_image.cc",
        "nodejs_wrapper/resource_io_group.cc",
        "nodejs_wrapper/resource_io.cc",
        "nodejs_wrapper/wrapped_stress_test_resource_io.cc",
        "nodejs_wrapper/wrapped_video_reader.cc",
        "utils.cc",
        "image_interface.cc",
        "video_reader.cc",
        "custom_io_group.cc",
        "private/custom_io_setup.cc",
        "private/stream_map.cc",
        "private/utils.cc",
        "private/volume_data.cc",
      ],
      "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<!@(./find_libav.sh -i)",
      ],
      'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
      "libraries": [
        "<!(./find_libav.sh -l)",
        "-lavformat",
        "-lavutil",
        "-lswresample",
        "-lswscale",
        "-lavcodec",
      ],
      "cflags_cc": [
        "-g",
        "-fPIC",
        "-Wno-unused-function",
        "-Wno-unknown-pragmas",
        "-DNAPI_DISABLE_CPP_EXCEPTIONS",
      ],
    }
  ]
}
