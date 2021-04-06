// ScreenRecorder.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "Queue.h"
#include "VideoEncoder.h"
#include "ScreenGrabber.h"



struct consumerThreadParams
{
	VideoEncoder *video_writer;
	Queue<BYTE*> *frameQueue;
};

struct producerThreadParams
{
	ScreenGrabber *screenshoter;
	VideoEncoder *video_writer;
	Queue<BYTE*> *frameQueue;
};


/* 
/ grab a screenshot and put it in the back of the thead-safe Queue 
*/
DWORD WINAPI producer(LPVOID threadParam)
{
	producerThreadParams *prodcStruct = (producerThreadParams *)threadParam;

	Queue<BYTE*> *frameQueue = prodcStruct->frameQueue;
	ScreenGrabber *screenshoter = prodcStruct->screenshoter;

	BYTE* newFrame = screenshoter->GetScreenshot();
	frameQueue->push(newFrame);

	//std::cout << "Just pushed a frame in Queue! Queue-size: " << frameQueue->size() << std::endl;
	return 0;
}



DWORD WINAPI consumer(LPVOID threadParam)
{
	consumerThreadParams *consStruct = (consumerThreadParams *)threadParam;

	Queue<BYTE*> *frameQueue = consStruct->frameQueue;
	VideoEncoder *video_writer = consStruct->video_writer;

	UINT32 totalFrames = video_writer->get_frames();

	for (unsigned i = 0; i < totalFrames; ++i)
	{
		auto frame = frameQueue->pop();
		video_writer->WriteFrame(frame);

		//std::this_thread::sleep_for(std::chrono::microseconds(2000));
	}

	std::cout << "Finished encoding/writing video!"  << std::endl;
	return 0;
}


int main()
{
	// the thread-safe queue
	Queue<BYTE*> frameQueue;

	// user's options
	INT32 framerate = 20;
	INT32 record_duration = 45;		   // in seconds
	INT32 totalFrames = framerate * record_duration;
	UINT32 avg_bitrate = 5000000;	     // no apparent configuration for lossless so far
	GUID codec = MFVideoFormat_H264;    // MFVideoFormat from mfapi.h
	LPCWSTR filename = L"screen-recording.mp4";
	LONG screenWidth, screenHeight;

	// WinAPI accurate timing
	LARGE_INTEGER frequency;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	double elapsedSeconds;
	QueryPerformanceFrequency(&frequency);

	ScreenGrabber screenshoter;
	screenWidth = screenshoter.get_width();
	screenHeight = screenshoter.get_height();

	VideoEncoder video_writer(codec, framerate, record_duration, screenWidth, screenHeight, avg_bitrate, filename);
	video_writer.InitVideoEncoder();


	// consumer's thread parameters
	consumerThreadParams consStruct;
	consStruct.frameQueue = &frameQueue;
	consStruct.video_writer = &video_writer;

	// producers's thread parameters
	producerThreadParams prodcStruct;
	prodcStruct.frameQueue = &frameQueue;
	prodcStruct.screenshoter = &screenshoter;
	prodcStruct.video_writer = &video_writer;


	/* The management of threads is performed with low-level C system calls
	   https://docs.microsoft.com/el-gr/windows/desktop/ProcThread/scheduling-priorities */


	// Windows thread handlers
	HANDLE producerThread, consumerThread;
	DWORD producerThreadID, consumerThreadID;

	// Start the consumer thread (which encodes video frames) this thread should busy-wait upon an empty Queue
	consumerThread = CreateThread(0, 0, consumer, (LPVOID)&consStruct, 0, &consumerThreadID);
	SetThreadPriority(consumerThread, THREAD_PRIORITY_NORMAL);

	// this is a "technique" in order to achieve the desired FPS
	std::chrono::microseconds frameT{ 1000000 / video_writer.get_FPS() };
	typedef std::chrono::high_resolution_clock HRclock;
	HRclock::time_point startTime = HRclock::now();

	QueryPerformanceCounter(&start);

	// Detach the screenshot-producer threads 
	for (int i = 0; i < totalFrames; ++i)
	{
		producerThread = CreateThread(0, 0, producer, (LPVOID)&prodcStruct, 0, &producerThreadID);

		SetThreadPriority(producerThread, THREAD_PRIORITY_HIGHEST);

		// sleep for a small time-frame according to the desired FPS
		std::this_thread::sleep_until(startTime+(i+1)*frameT);
	}


	QueryPerformanceCounter(&end);
	elapsedSeconds = (end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
	std::cout << "Time passed: " << elapsedSeconds << " seconds" << std::endl;


	WaitForSingleObject(consumerThread, INFINITE);

	video_writer.shutdownWriter();
	

}