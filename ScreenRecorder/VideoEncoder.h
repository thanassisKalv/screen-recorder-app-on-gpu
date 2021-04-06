#pragma once

#include "stdafx.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>


#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfuuid")

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}


/* reference: https://docs.microsoft.com/en-us/windows/desktop/cossdk/interpreting-error-codes */
void ErrorDescription(HRESULT hr)
{
	if (FACILITY_WINDOWS == HRESULT_FACILITY(hr))
		hr = HRESULT_CODE(hr);
	TCHAR* szErrMsg;

	if (FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&szErrMsg, 0, NULL) != 0)
	{
		_tprintf(TEXT("%s"), szErrMsg);
		LocalFree(szErrMsg);
	}
	else
		_tprintf(TEXT("[Could not find a description for error # %#x.]\n"), hr);
}


class VideoEncoder
{

public:

	// constructor, user's parameters definition
	VideoEncoder(const GUID encoding_format, UINT32 fps, UINT32 duration, LONG width, LONG height, UINT32 avg_bitrate, LPCWSTR filename)
	{
		VIDEO_ENCODING_FORMAT = encoding_format;
		VIDEO_FPS = fps;
		DURATION = duration;
		VIDEO_FRAME_COUNT = fps * duration;
		VIDEO_FRAME_DURATION = 10 * 1000 * 1000 / fps;
		FILENAME = filename;
		VIDEO_BIT_RATE = avg_bitrate;
		VIDEO_WIDTH = width;
		VIDEO_HEIGHT = height;
		VIDEO_PIXELS = width * height;
	};

	// Copy constructor. Don't implement.
	VideoEncoder(const VideoEncoder &) = delete;

	// Assignment operator. Don't implement.
	VideoEncoder& operator= (const VideoEncoder &) = delete;

	/*
		Method to initialize the input & output Mediatype streams in a format according 
		to the users parameters
		The 'Microsoft Media Foundation' methods
		IMFSinkWriter::AddStream
		IMFSinkWriter::SetInputMediaType
		IMFSinkWriter::BeginWriting
	*/
	void InitVideoEncoder()
	{
		HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (SUCCEEDED(hr))
		{
			hr = MFStartup(MF_VERSION);
			if (SUCCEEDED(hr))
			{

				IMFSinkWriter   *tmp_pSinkWriter = NULL;
				IMFMediaType    *pMediaTypeOut = NULL;
				IMFMediaType    *pMediaTypeIn = NULL;
				DWORD           streamIndex;

				HRESULT hr = MFCreateSinkWriterFromURL(FILENAME, NULL, NULL, &tmp_pSinkWriter);

				// Set the output media type.
				if (SUCCEEDED(hr)){
					hr = MFCreateMediaType(&pMediaTypeOut);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeOut->SetGUID(MF_MT_SUBTYPE, VIDEO_ENCODING_FORMAT);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, VIDEO_BIT_RATE);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
				}
				if (SUCCEEDED(hr)) {
					if(VIDEO_ENCODING_FORMAT == MFVideoFormat_H264)
						hr = pMediaTypeOut->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
					else if (VIDEO_ENCODING_FORMAT == MFVideoFormat_HEVC)
						hr = pMediaTypeOut->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH265VProfile_Main_420_8);
				}
				if (SUCCEEDED(hr)){
					hr = MFSetAttributeSize(pMediaTypeOut, MF_MT_FRAME_SIZE, VIDEO_WIDTH, VIDEO_HEIGHT);
				}
				if (SUCCEEDED(hr)){
					hr = MFSetAttributeRatio(pMediaTypeOut, MF_MT_FRAME_RATE, VIDEO_FPS, 1);
				}
				if (SUCCEEDED(hr)){
					hr = MFSetAttributeRatio(pMediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
				}
				if (SUCCEEDED(hr)){
					hr = tmp_pSinkWriter->AddStream(pMediaTypeOut, &streamIndex);
				}

				//------------------
				// Set the input media type.
				if (SUCCEEDED(hr)){
					hr = MFCreateMediaType(&pMediaTypeIn);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeIn->SetGUID(MF_MT_SUBTYPE, VIDEO_INPUT_FORMAT);
				}
				if (SUCCEEDED(hr)){
					hr = pMediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
				}
				if (SUCCEEDED(hr)){
					hr = MFSetAttributeSize(pMediaTypeIn, MF_MT_FRAME_SIZE, VIDEO_WIDTH, VIDEO_HEIGHT);
				}
				if (SUCCEEDED(hr)){
					hr = MFSetAttributeRatio(pMediaTypeIn, MF_MT_FRAME_RATE, VIDEO_FPS, 1);
				}
				if (SUCCEEDED(hr)){
					hr = MFSetAttributeRatio(pMediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
				}
				if (SUCCEEDED(hr)){
					hr = tmp_pSinkWriter->SetInputMediaType(streamIndex, pMediaTypeIn, NULL);
				}

				// Tell the sink writer to start accepting data.
				if (SUCCEEDED(hr))
				{
					hr = tmp_pSinkWriter->BeginWriting();
				}

				// Return the pointer to the caller.
				if (SUCCEEDED(hr))
				{
					pSinkWriter = tmp_pSinkWriter;
					pSinkWriter->AddRef();
					stream = streamIndex;
				}

				// print possible HRESULT error_string
				if (!SUCCEEDED(hr))
					ErrorDescription(hr);

				SafeRelease(&tmp_pSinkWriter);
				SafeRelease(&pMediaTypeOut);
				SafeRelease(&pMediaTypeIn);
				return;
			}

		}

	}

	// helper function
	void rgb2yuv(BYTE *yuvFrame, BYTE *rgbFrame)
	{

		// ------ CONVERT the RGB bitmap screenshot to YV12 -------
		int yIndex = 0;
		int frameSize = VIDEO_WIDTH * VIDEO_HEIGHT;
		int uIndex = frameSize;
		int vIndex = frameSize + (frameSize / 4);

		int  R, G, B, Y, U, V;
		int index = 0;
		int total = VIDEO_HEIGHT * VIDEO_WIDTH * 3;
		for (unsigned j = 0; j < VIDEO_HEIGHT; j++)
		{
			for (unsigned i = 0; i < VIDEO_WIDTH; i++)
			{
				R = rgbFrame[(total - (1 + index / VIDEO_WIDTH)*VIDEO_WIDTH * 3) + (i * 3 + 2)];
				G = rgbFrame[(total - (1 + index / VIDEO_WIDTH)*VIDEO_WIDTH * 3) + (i * 3 + 1)];
				B = rgbFrame[(total - (1 + index / VIDEO_WIDTH)*VIDEO_WIDTH * 3) + (i * 3 + 0)];

				// RGB to YUV algorithm
				Y = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
				V = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128; // Previously U
				U = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128; // Previously V

				yuvFrame[yIndex++] = (byte)((Y < 0) ? 0 : ((Y > 255) ? 255 : Y));
				if (j % 2 == 0 && index % 2 == 0) {
					yuvFrame[vIndex++] = (byte)((V < 0) ? 0 : ((V > 255) ? 255 : V));
					yuvFrame[uIndex++] = (byte)((U < 0) ? 0 : ((U > 255) ? 255 : U));
				}
				index++;
			}
		}
	}

	/*
		Puts the screenshot buffer to a 'IMFMediaBuffer'and calls IMFSinkWriter::WriteSample()

		*N1: the rtStart is incremented in fixed steps, this may need to re-consider
		*N2: the change the interface of the function to accept and manipulates buffers according
			 to screenshot-input and encoder-input formats  (MFVideoFormat_YV12, MFVideoFormat_RGB etc...) 
	*/
	void WriteFrame(BYTE *currentFrame)
	{
		BYTE *videoFrameBuffer;
		videoFrameBuffer = new BYTE[(UINT64)VIDEO_PIXELS * 3];

		rgb2yuv(videoFrameBuffer, currentFrame);

		IMFSample *pSample = NULL;
		IMFMediaBuffer *pBuffer = NULL;

		const LONG cbWidth = 3 * VIDEO_WIDTH;
		const DWORD cbBuffer = cbWidth * VIDEO_HEIGHT;

		BYTE *pData = NULL;

		// Create a new memory buffer.
		HRESULT hr = MFCreateMemoryBuffer(cbBuffer, &pBuffer);

		// Lock the buffer and copy the video frame to the buffer.
		if (SUCCEEDED(hr))
		{
			hr = pBuffer->Lock(&pData, NULL, NULL);
		}
		if (SUCCEEDED(hr))
		{
			hr = MFCopyImage(
				pData,                      // Destination buffer.
				cbWidth,                    // Destination stride.
				(BYTE*)videoFrameBuffer,	// **First row in source image.
				cbWidth,                    // Source stride.
				cbWidth,                    // Image width in bytes.
				VIDEO_HEIGHT                // Image height in pixels.
			);
		}
		if (pBuffer)
		{
			pBuffer->Unlock();
		}

		// Set the data length of the buffer.
		if (SUCCEEDED(hr))
		{
			hr = pBuffer->SetCurrentLength(cbBuffer);
		}

		// Create a media sample and add the buffer to the sample.
		if (SUCCEEDED(hr))
		{
			hr = MFCreateSample(&pSample);
		}
		if (SUCCEEDED(hr))
		{
			hr = pSample->AddBuffer(pBuffer);
		}

		// Set the time stamp and the duration.
		if (SUCCEEDED(hr))
		{
			hr = pSample->SetSampleTime(rtStart);
		}
		if (SUCCEEDED(hr))
		{
			hr = pSample->SetSampleDuration(VIDEO_FRAME_DURATION);
		}

		// Send the sample to the Sink Writer.
		if (SUCCEEDED(hr))
		{
			hr = pSinkWriter->WriteSample(stream, pSample);
		}

		SafeRelease(&pSample);
		SafeRelease(&pBuffer);

		rtStart += VIDEO_FRAME_DURATION;

		free(currentFrame);
		free(videoFrameBuffer);
		
		return;
	}

	UINT32 get_frames() 
	{
		return VIDEO_FRAME_COUNT;
	}

	UINT32 get_FPS()
	{
		return VIDEO_FPS;
	}

	void shutdownWriter()
	{
		pSinkWriter->Finalize();
		SafeRelease(&pSinkWriter);
		MFShutdown();
		CoUninitialize();
	};

private:

	// main object
	IMFSinkWriter *pSinkWriter = NULL;
	// stream variables
	DWORD stream;
	LONGLONG rtStart = 0;

	// Format definition
	UINT32 VIDEO_WIDTH = 1920;
	UINT32 VIDEO_HEIGHT = 1080;
	UINT32 VIDEO_FPS = 25;
	UINT32 DURATION = 12;
	UINT64 VIDEO_FRAME_DURATION = 10 * 1000 * 1000 / VIDEO_FPS;
	UINT32 VIDEO_BIT_RATE = 4000000;
	GUID   VIDEO_ENCODING_FORMAT = MFVideoFormat_H264;
	GUID   VIDEO_INPUT_FORMAT = MFVideoFormat_YV12;
	UINT32 VIDEO_PIXELS = VIDEO_WIDTH * VIDEO_HEIGHT;
	UINT32 VIDEO_FRAME_COUNT;
	LPCWSTR FILENAME = L"screen-recording.mp4";
};

