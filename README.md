# screen-recorder-app-on-gpu
a screen recording Windows application based on Nvidia's NvCodec and D3D11 API. The code illustrates a GPU-based realtime pipeline that generates stream of screenshots as ID3D11Texture2D textures and uses them on the fly as an raw frame input to [NvCodec H264-encoder](https://github.com/NVIDIA/video-sdk-samples/blob/master/Samples/NvCodec/NvEncoder/NvEncoderD3D11.h) .<br/> <br/> 
Prior to this implementation, another Windows-based but much less efficient version of the [screen recorder](https://github.com/thanassisKalv/screen-recorder-app-on-gpu/tree/cpu-version-wingdi) was developed

## System requirements
 - project has been saved with VS2017, the Direct X SDK should be installed
 - minumum software & hardware requirements are as reported in NVIDIA's "video-sdk-samples" README

## Implementation summary
The solution is built on top of this [application from NVIDIA's "video-sdk-samples"](https://github.com/NVIDIA/video-sdk-samples/blob/master/Samples/AppEncode/AppEncD3D11/AppEncD3D11.cpp).
Specifically, the solution implements the *producer-consumer pattern* along with a thread-safe Queue implementation to manage the screen frames.
About command line usage:
 - the Nvidia's utils function for command line parsing is modified to accept -dur argument (duration in seconds)
 - to explore the typical video encoding options you can call it with -h


Credits to: the research done by Diederickh for Windows OS high-performance GPU frame-capturing using IDXGIOutputDuplication in this [repository](https://github.com/diederickh/screen_capture/blob/master/src/test/test_win_api_directx_research.cpp)


### to do (high level)
 - omit the fixed duration argument and implement record stopping functionality by capturing specific keys
 - integrate some piece of code for transcoding the raw .h264 into a container format (mp4/mkv)
 
### to do (low level)
 - move the frame-writing out of the encoder loop in the Consumer function
