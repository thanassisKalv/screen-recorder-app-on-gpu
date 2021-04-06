/*
* Copyright 2017-2018 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#include <d3d11.h>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <wrl.h>
#include "NvEncoder/NvEncoderD3D11.h"
#include "./Utils/Logger.h"
#include "./Utils/NvCodecUtils.h"
#include "./Common/AppEncUtils.h"
#include <DXGI.h>
#include <DXGI1_2.h> /* For IDXGIOutput1 */

#include "Queue.h"
#include <thread>

#define FPS_CAPTURE_INTERVAL 33
#define FPS_DEFAULT 30

#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;


simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

class RGBToNV12ConverterD3D11 
{
public:
    RGBToNV12ConverterD3D11(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, int nWidth, int nHeight)
        : pD3D11Device(pDevice), pD3D11Context(pContext)
    {
        pD3D11Device->AddRef();
        pD3D11Context->AddRef();

        pTexBgra = NULL;
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
        desc.Width = nWidth;
        desc.Height = nHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        ck(pDevice->CreateTexture2D(&desc, NULL, &pTexBgra));

        ck(pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void **)&pVideoDevice));
        ck(pContext->QueryInterface(__uuidof(ID3D11VideoContext), (void **)&pVideoContext));

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = 
        {
            D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
            { 1, 1 }, desc.Width, desc.Height,
            { 1, 1 }, desc.Width, desc.Height,
            D3D11_VIDEO_USAGE_PLAYBACK_NORMAL
        };
        ck(pVideoDevice->CreateVideoProcessorEnumerator(&contentDesc, &pVideoProcessorEnumerator));

        ck(pVideoDevice->CreateVideoProcessor(pVideoProcessorEnumerator, 0, &pVideoProcessor));
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = { 0, D3D11_VPIV_DIMENSION_TEXTURE2D, { 0, 0 } };
        ck(pVideoDevice->CreateVideoProcessorInputView(pTexBgra, pVideoProcessorEnumerator, &inputViewDesc, &pInputView));
    }

    ~RGBToNV12ConverterD3D11()
    {
        for (auto& it : outputViewMap)
        {
            ID3D11VideoProcessorOutputView* pOutputView = it.second;
            pOutputView->Release();
        }

        pInputView->Release();
        pVideoProcessorEnumerator->Release();
        pVideoProcessor->Release();
        pVideoContext->Release();
        pVideoDevice->Release();
        pTexBgra->Release();
        pD3D11Context->Release();
        pD3D11Device->Release();
    }
    
	void ConvertRGBToNV12(ID3D11Texture2D*pRGBSrcTexture, ID3D11Texture2D* pDestTexture)
    {
        pD3D11Context->CopyResource(pTexBgra, pRGBSrcTexture);
        ID3D11VideoProcessorOutputView* pOutputView = nullptr;
        auto it = outputViewMap.find(pDestTexture);
        if (it == outputViewMap.end())
        {
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = { D3D11_VPOV_DIMENSION_TEXTURE2D };
            ck(pVideoDevice->CreateVideoProcessorOutputView(pDestTexture, pVideoProcessorEnumerator, &outputViewDesc, &pOutputView));
            outputViewMap.insert({ pDestTexture, pOutputView });
        }
        else
        {
            pOutputView = it->second;
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream = { TRUE, 0, 0, 0, 0, NULL, pInputView, NULL };
        ck(pVideoContext->VideoProcessorBlt(pVideoProcessor, pOutputView, 0, 1, &stream));
        return;
    }

	ComPtr<IDXGIOutputDuplication> init_duplication(ID3D11Device *pDevice)
	{
		ComPtr<IDXGIFactory1> factory;
		ComPtr<IDXGIAdapter> adapter;
		//IDXGIOutput* output = NULL;
		ComPtr<IDXGIOutput> output;
		ComPtr<IDXGIOutput1> output1;
		ComPtr<IDXGIOutputDuplication> duplication;

		ck(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)factory.GetAddressOf()));
		ck(factory->EnumAdapters(0, adapter.GetAddressOf()));

		std::cout << "Initialized duplication stream\n";

		ck(adapter->EnumOutputs(0, output.GetAddressOf()));
		ck(output->QueryInterface(__uuidof(IDXGIOutput1), (void **)output1.GetAddressOf()));

		ck(output1->DuplicateOutput(pDevice, duplication.GetAddressOf()));
		DXGI_OUTDUPL_DESC duplication_desc;
		duplication->GetDesc(&duplication_desc);
			

		return duplication;
	}

private:
    ID3D11Device *pD3D11Device = NULL;
    ID3D11DeviceContext *pD3D11Context = NULL;
    ID3D11VideoDevice *pVideoDevice = NULL;
    ID3D11VideoContext *pVideoContext = NULL;
    ID3D11VideoProcessor *pVideoProcessor = NULL;
    ID3D11VideoProcessorInputView *pInputView = NULL;
    ID3D11VideoProcessorOutputView *pOutputView = NULL;
    ID3D11Texture2D *pTexBgra = NULL;
    ID3D11VideoProcessorEnumerator *pVideoProcessorEnumerator = nullptr;
    std::unordered_map<ID3D11Texture2D*, ID3D11VideoProcessorOutputView*> outputViewMap;
};


struct producerThreadParams
{
	Queue<std::vector<std::vector<uint8_t>>*> *frameQueue;
	ComPtr<IDXGIOutputDuplication> duplication;
	ComPtr<ID3D11DeviceContext> pContext;
	Queue<UINT8> *waitQueue;
	NvEncoderD3D11 *enc;
	int totalFrames;
};


DWORD WINAPI frameProducer(LPVOID threadParam)
{
	producerThreadParams *prodStruct = (producerThreadParams *)threadParam;

	Queue<std::vector<std::vector<uint8_t>>*> *frameQueue = prodStruct->frameQueue;
	Queue<UINT8> *waitQueue = prodStruct->waitQueue;
	ComPtr<IDXGIOutputDuplication> duplication = prodStruct->duplication;
	NvEncoderD3D11 *enc = prodStruct->enc;
	ComPtr<ID3D11DeviceContext> pContext = prodStruct->pContext;

	DXGI_OUTDUPL_FRAME_INFO frame_info;
	DXGI_MAPPED_RECT mapped_rect;

	typedef std::chrono::system_clock sclock;
	std::chrono::microseconds period{ 1000000 / FPS_DEFAULT };

	UINT32 frames = 0;

	while (frames < prodStruct->totalFrames)
	{
		std::cout << frameQueue->size() << " frame captured" << std::endl;
		ComPtr<IDXGIResource> desktop_resource;
		ComPtr<ID3D11Texture2D> screenTex;
		std::vector<std::vector<uint8_t>> vPacket;

		sclock::time_point currentTime = sclock::now();

		duplication->AcquireNextFrame(1000, &frame_info, desktop_resource.GetAddressOf());
		ck(desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)screenTex.GetAddressOf()));
		duplication->MapDesktopSurface(&mapped_rect);

		// now the NvEncoderD3D11 is in a state that waits for the next gpu frame to be processed
		const NvEncInputFrame* encoderInputFrame = enc->GetNextInputFrame();
		// the encoderInputFrame->inputPtr needs firstly to reinterpret_cast its empty pointer and the gpu D3D11 texture will be copied into it
		ID3D11Texture2D *pTexBgra = reinterpret_cast<ID3D11Texture2D*>(encoderInputFrame->inputPtr);
		pContext->CopyResource(pTexBgra, screenTex.Get());

		frameQueue->push(&vPacket);

		sclock::time_point nextTime = currentTime + period;

		frames++;

		std::this_thread::sleep_until(nextTime);

		waitQueue->pop();

		duplication->ReleaseFrame();

	}

	std::cout << "There are " << frameQueue->size() << " frames remaining in queue" << std::endl;
	return 0;
}

struct consumerThreadParams
{
	Queue<std::vector<std::vector<uint8_t>>*> *frameQueue;
	NvEncoderD3D11 *enc;
	std::ofstream *fpOutWriter;
	Queue<UINT8> *waitQueue;
	int totalFrames;
};

DWORD WINAPI frameConsumer(LPVOID threadParam)
{
	consumerThreadParams *consStruct = (consumerThreadParams *)threadParam;

	Queue<std::vector<std::vector<uint8_t>>*> *frameQueue = consStruct->frameQueue;
	NvEncoderD3D11 *enc = consStruct->enc;
	std::ofstream *fpOut = consStruct->fpOutWriter;
	Queue<UINT8> *waitQueue = consStruct->waitQueue;

	UINT32 frames = 0;
	UINT8 done = 1;

	while (frames < consStruct->totalFrames)
	{
		// queue's "frame" is actually an empty frame, that signals the consumer that the NvEncoderD3D11 has a gpu frame ready for encoding
		auto frame = frameQueue->pop();

		if(frames < consStruct->totalFrames-1)
			enc->EncodeFrame(*frame);
		else
			enc->EndEncode(*frame);

		std::cout << frames << " frame encoded" << std::endl;

		// the frame-to-disk writing part of code will be highly more efficient to get out of this loop
		for (std::vector<uint8_t> &packet : *frame){
			fpOut->write(reinterpret_cast<char*>(packet.data()), packet.size());
		}

		frames++;
		
		std::this_thread::sleep_for(std::chrono::microseconds(1000));
		waitQueue->push(done); // inform the producer thread that the frame is processed
	}

	std::cout << "Finished encoding/writing of video!" << std::endl;
	return 0;
}

void Screens2Video(int nWidth, int nHeight, char *szOutFilePath, NvEncoderInitParam *pEncodeCLIOptions, int iGpu, int duration)
{
	Queue<std::vector<std::vector<uint8_t>>*> frameQueue;
	Queue<UINT8> waitQueue;

    ComPtr<ID3D11Device> pDevice;
    ComPtr<ID3D11DeviceContext> pContext;
    ComPtr<IDXGIFactory1> pFactory;
    ComPtr<IDXGIAdapter> pAdapter;
    ComPtr<ID3D11Texture2D> pTexSysMem;

	// D3D11 adapter GPU resource allocation - this will feed the NvEncoderD3D11 encoder
    ck(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)pFactory.GetAddressOf()));
    ck(pFactory->EnumAdapters(iGpu, pAdapter.GetAddressOf()));
    ck(D3D11CreateDevice(pAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        NULL, 0, D3D11_SDK_VERSION, pDevice.GetAddressOf(), NULL, pContext.GetAddressOf()));
    DXGI_ADAPTER_DESC adapterDesc;
    pAdapter->GetDesc(&adapterDesc);
    char szDesc[80];
    wcstombs(szDesc, adapterDesc.Description, sizeof(szDesc));
    std::cout << "GPU in use: " << szDesc << std::endl;

	// Following parameters determine the D3D11 texture grabbing
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
    desc.Width = nWidth;
    desc.Height = nHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ck(pDevice->CreateTexture2D(&desc, NULL, pTexSysMem.GetAddressOf()));

    std::unique_ptr<RGBToNV12ConverterD3D11> pConverter;

    NvEncoderD3D11 enc(pDevice.Get(), nWidth, nHeight, NV_ENC_BUFFER_FORMAT_ARGB);

    NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
    NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
    initializeParams.encodeConfig = &encodeConfig;
    enc.CreateDefaultEncoderParams(&initializeParams, pEncodeCLIOptions->GetEncodeGUID(), pEncodeCLIOptions->GetPresetGUID());

    pEncodeCLIOptions->SetInitParams(&initializeParams, NV_ENC_BUFFER_FORMAT_ARGB);

    enc.CreateEncoder(&initializeParams);

	// **** DUPLICATION
	ComPtr<IDXGIOutputDuplication> duplication = pConverter->init_duplication(pDevice.Get());
	DXGI_MAPPED_RECT mapped_rect;
	// ****


    std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);
    if (!fpOut){
        std::ostringstream err;
        err << "Unable to open output file: " << szOutFilePath << std::endl;
        throw std::invalid_argument(err.str());
    }

	HANDLE producerThread, consumerThread;
	DWORD producerThreadID, consumerThreadID;

	LARGE_INTEGER frequency;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	double elapsedSeconds;

    int nSize = nWidth * nHeight * 4;
    std::unique_ptr<uint8_t[]> pHostFrame(new uint8_t[nSize]);
	int totalFrames = FPS_DEFAULT * duration;
    int nFrame = 0;
	int idx = 0;

	// prepare the consumer's (video-writer) thread parameters
	consumerThreadParams consStruct;
	consStruct.frameQueue = &frameQueue;
	consStruct.fpOutWriter = &fpOut;
	consStruct.totalFrames = totalFrames;
	consStruct.enc = &enc;
	consStruct.waitQueue = &waitQueue;

	consumerThread = CreateThread(0, 0, frameConsumer, (LPVOID)&consStruct, 0, &consumerThreadID);
	SetThreadPriority(consumerThread, THREAD_PRIORITY_HIGHEST);

	// prepare the producer's (video-writer) thread parameters
	producerThreadParams prodStruct;
	prodStruct.frameQueue = &frameQueue;
	prodStruct.duplication = duplication;
	prodStruct.enc = &enc;
	prodStruct.totalFrames = totalFrames;
	prodStruct.pContext = pContext;
	prodStruct.waitQueue = &waitQueue;

	// producer thread is of "critical" priority because of desired FPS
	producerThread = CreateThread(0, 0, frameProducer, (LPVOID)&prodStruct, 0, &producerThreadID);
	SetThreadPriority(producerThread, THREAD_PRIORITY_HIGHEST);

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);

	// Firstly wait for frameProducer to return and then for frameConsumer
	WaitForSingleObject(producerThread, INFINITE);

	QueryPerformanceCounter(&end);
	elapsedSeconds = (end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
	std::cout << "Elapsed recording time: " << elapsedSeconds << " seconds" << std::endl;

	WaitForSingleObject(consumerThread, INFINITE);

	enc.DestroyEncoder();

	// close the recording file
    fpOut.close();

    std::cout << "Recording has finished " << std::endl << "Saved in file " << szOutFilePath << std::endl;
}



/**
	This screen-recording application is implemented on Producer–Consumer pattern using a thread-safe Queue
*
	This demo screen-recording solution that mainly illustrates a GPU-based pipeline for grabbing and H264 video encoding of frames capturing ID3D11Texture2D textures.
*
	the high-performance capturing using IDXGIOutputDuplication was researched by Diederickh in https://github.com/diederickh/screen_capture/blob/master/src/test/test_win_api_directx_research.cpp
*
	It is built on top of this app from NVIDIA's "video-sdk-samples"  https://github.com/NVIDIA/video-sdk-samples/blob/master/Samples/AppEncode/AppEncD3D11/AppEncD3D11.cpp 
*/
int main(int argc, char **argv)
{

    char szOutFilePath[256] = "screenRecording.h264";
    int nWidth = 1920, nHeight = 1080;
	int duration = 20;

    try
    {
        NvEncoderInitParam encodeCLIOptions;
        int iGpu = 0;
        ParseCommandLine_AppEncD3D(argc, argv, nWidth, nHeight, szOutFilePath, encodeCLIOptions, iGpu, duration);

        Screens2Video( nWidth, nHeight, szOutFilePath, &encodeCLIOptions, iGpu, duration);
    }
    catch (const std::exception &ex)
    {
        std::cout << ex.what();
        exit(1);
    }
    return 0;
}
