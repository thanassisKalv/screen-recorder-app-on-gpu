
#include "stdafx.h"
#include <mutex>


/*
	A <Wingdi.h> dependent bitmap-screenshot generator
	This may not be the fastest option, especially compared to an efficient 'DirectX' implementation
	Note that this implementation may limit our upper possible FPS for screen recording
*/
class ScreenGrabber
{

public:

	/*
		constructor initializes a Bitmap Header by querying the screen's resolution 
	*/
	ScreenGrabber()
	{
		ZeroMemory(&bfHeader, sizeof(BITMAPFILEHEADER));
		ZeroMemory(&biHeader, sizeof(BITMAPINFOHEADER));
		ZeroMemory(&bInfo, sizeof(BITMAPINFO));
		ZeroMemory(&bAllDesktops, sizeof(BITMAP));

		hDC = GetDC(NULL);
		hTempBitmap = GetCurrentObject(hDC, OBJ_BITMAP);
		GetObjectW(hTempBitmap, sizeof(BITMAP), &bAllDesktops);

		lWidth = bAllDesktops.bmWidth;
		lHeight = bAllDesktops.bmHeight;
		Pixels = lWidth*lHeight;

		DeleteObject(hTempBitmap);

		bfHeader.bfType = (WORD)('B' | ('M' << 8));
		bfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
		biHeader.biSize = sizeof(BITMAPINFOHEADER);
		biHeader.biBitCount = 24;
		biHeader.biCompression = BI_RGB;
		biHeader.biPlanes = 1;
		biHeader.biWidth = lWidth;
		biHeader.biHeight = lHeight;

		bInfo.bmiHeader = biHeader;

		cbBits = (((24 * lWidth + 31)&~31) / 8) * lHeight;
		bBits = (BYTE *)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, cbBits);
		//std::cout << cbBits << std::endl;
		if (bBits == NULL)
		{
			MessageBoxW(NULL, L"Out of memory", L"Error", MB_OK | MB_ICONSTOP);
			return;
		}

		hMemDC = CreateCompatibleDC(hDC);
		hBitmap = CreateDIBSection(hDC, &bInfo, DIB_RGB_COLORS, (VOID **)&bBits, NULL, 0);
		SelectObject(hMemDC, hBitmap);
	}

	/*
		thread-safe screenshot taker
		keep the *bBits accessible only to one thread
	*/
	BYTE* GetScreenshot()
	{
		BYTE *videoFrameBuffer;
		videoFrameBuffer = new BYTE[Pixels * 3];

		std::unique_lock<std::mutex> mlock(mutex_);
		
		BitBlt(hMemDC, 0, 0, lWidth, lHeight, hDC, 0, 0, SRCCOPY);

		memcpy(videoFrameBuffer, bBits, Pixels * 3);

		mlock.unlock();

		return videoFrameBuffer;
	}

	LONG get_width()
	{
		return lWidth;
	}

	LONG get_height()
	{
		return lHeight;
	}

private:

	std::mutex mutex_;

	BITMAPFILEHEADER bfHeader;
	BITMAPINFOHEADER biHeader;
	BITMAPINFO bInfo;
	HGDIOBJ hTempBitmap;
	HBITMAP hBitmap;
	BITMAP bAllDesktops;
	HDC hDC, hMemDC;
	LONG lWidth, lHeight;
	LONG Pixels;
	BYTE *bBits = NULL;
	HANDLE hHeap = GetProcessHeap();
	DWORD cbBits, dwWritten = 0;
	
};