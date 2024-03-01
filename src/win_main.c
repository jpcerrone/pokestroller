#include "walker.h"
#include "vector2i.h"

#include <assert.h>
#include <Windows.h>
#include <stdint.h>
#include <stdio.h>

#define TICKS_PER_SEC 4 /* RTC/4 */
static HCURSOR cursor;
WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };

static bool walkerRunning;

float getEllapsedSeconds(LARGE_INTEGER endPerformanceCount, LARGE_INTEGER startPerformanceCount, LARGE_INTEGER performanceFrequency) {
	LARGE_INTEGER ellapsedMicroSeconds;
	ellapsedMicroSeconds.QuadPart = endPerformanceCount.QuadPart - startPerformanceCount.QuadPart;
	//ellapsedMicroSeconds.QuadPart *= 1000000;
	return (float)ellapsedMicroSeconds.QuadPart / (float)performanceFrequency.QuadPart;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LRESULT returnVal = 0;
    switch (uMsg)
    {
		case WM_SIZE:{
		} break;
		case WM_SETCURSOR: {
			SetCursor(cursor);
			} break;
		case WM_CLOSE: {
			walkerRunning = false;
			DestroyWindow(hwnd);
		} break;
		default: {
			returnVal = DefWindowProc(hwnd, uMsg, wParam, lParam);
		} break;
    }
    return returnVal;
}

// hInstance: handle to the .exe
// hPrevInstance: not used since 16bit windows
// WINAPI: calling convention, tells compiler order of parameters
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	struct Vector2i nativeRes = { LCD_WIDTH, LCD_HEIGHT };
	int scalingFactor = 4;

	//MMRESULT canQueryEveryMs = timeBeginPeriod(1);
	//assert(canQueryEveryMs == TIMERR_NOERROR);

	struct Vector2i windowsScreenRes = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
	// This already considers Window's scaling factor (ie 125%).

	struct Vector2i screenRes = { nativeRes.width * scalingFactor, nativeRes.height * scalingFactor };
	struct Vector2i origin = { (windowsScreenRes.width - screenRes.width)/2, (windowsScreenRes.height - screenRes.height) / 2 };
	BITMAPINFO bitmapInfo;
	BITMAPINFOHEADER bmInfoHeader = {0};
	bmInfoHeader.biSize = sizeof(bmInfoHeader);
	bmInfoHeader.biCompression = BI_RGB;
	bmInfoHeader.biWidth = nativeRes.width;
	bmInfoHeader.biHeight = -nativeRes.height; // Negative means it'll be filled top-down
	bmInfoHeader.biPlanes = 1;       // MSDN says it must be set to 1, legacy reasons
	bmInfoHeader.biBitCount = 32;    // R+G+B+padding each 8bits
	bitmapInfo.bmiHeader = bmInfoHeader;

	void* bitMapMemory;
	bitMapMemory = VirtualAlloc(0, nativeRes.width * nativeRes.height * 4 /*(4Bytes(32b) for color)*/, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	assert(SUCCEEDED(init));
	// Register the window class.
	const wchar_t CLASS_NAME[] = L"PokeStroller";

	WNDCLASS wc = {0};

	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = (LPCSTR)CLASS_NAME;
	cursor = LoadCursor(0, IDC_ARROW);
	wc.hCursor = cursor;// class cursor
	
	RegisterClass(&wc);
	RECT desiredClientSize;
	desiredClientSize.left = 0;
	desiredClientSize.right = screenRes.width;
	desiredClientSize.top = 0;
	desiredClientSize.bottom = screenRes.height;

	DWORD windowStyles = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
	AdjustWindowRectEx(&desiredClientSize, windowStyles, false, 0);
	HWND hwnd = CreateWindowEx(
		0,                              // Optional window styles.
		(LPCSTR)CLASS_NAME,                     // Window class
		"PokeStroller",    // Window text
		windowStyles,            // Window style

		// Size and position
		origin.x, origin.y, desiredClientSize.right - desiredClientSize.left, desiredClientSize.bottom - desiredClientSize.top,

		NULL,       // Parent window    
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);
	if (hwnd) {
		HDC windowDeviceContext = GetDC(hwnd);
		ShowWindow(hwnd, nShowCmd);
		
		initWalker();
		int instructionsToStep = 0;
		walkerRunning = true;
		// mode = RUN;
		bool enter = 0;
		bool left = 0;
		bool right = 0;
		uint64_t cycleCount = 0;
		// Timing
		LARGE_INTEGER performanceFrequency;
		QueryPerformanceFrequency(&performanceFrequency);

		LARGE_INTEGER startPerformanceCount;
		QueryPerformanceCounter(&startPerformanceCount);
		while (walkerRunning) {
			// Process Messages
			MSG msg = {0};
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				WPARAM key = msg.wParam;

				switch (msg.message) {
					case WM_KEYDOWN: {
						bool wasDown = msg.lParam & (1 << 30);
						if (key == VK_SPACE) {
							enter = true;
						}
						if (key == 'Z') {
							left = true;
						}
						if (key == 'X') {
							right = true;
						}                    
					} break;
					case WM_KEYUP: {
						if (key == VK_SPACE) {
							enter = false;
						}
						if (key == 'Z') {
							left = false;
						}
						if (key == 'X') {
							right = false;
						}
					}break;
					case WM_QUIT: {
						walkerRunning = false;
					} break;
					default: {
						TranslateMessage(&msg);
						DispatchMessage(&msg);
					} break;
				}

			}
			setKeys(enter, left, right);
			bool error = runNextInstruction(&cycleCount);
			if(error){
				walkerRunning = false; 
			}
			if (cycleCount >= SYSTEM_CLOCK_CYCLES_PER_SECOND/TICKS_PER_SEC){
				cycleCount -= SYSTEM_CLOCK_CYCLES_PER_SECOND/TICKS_PER_SEC;

				setRTCQuarterBit();

				fillVideoBuffer(bitMapMemory);
				StretchDIBits(windowDeviceContext, 0, 0, screenRes.width, screenRes.height, 0, 0, nativeRes.width, nativeRes.height, bitMapMemory, &bitmapInfo, DIB_RGB_COLORS, SRCCOPY);

				float desiredFrameTimeInS = 1.0f / TICKS_PER_SEC;
				LARGE_INTEGER endPerformanceCount;
				QueryPerformanceCounter(&endPerformanceCount);
				float elapsedSeconds = getEllapsedSeconds(endPerformanceCount, startPerformanceCount, performanceFrequency);
#ifdef DISPLAY_FRAME_TIME
				char str[20];
				sprintf(str, "%f\n", elapsedSeconds);
				OutputDebugStringA(str);
#endif
				if (elapsedSeconds < desiredFrameTimeInS) {
					DWORD timeToSleep = (DWORD)(1000.0f * (desiredFrameTimeInS - elapsedSeconds));
					Sleep(timeToSleep);
					QueryPerformanceCounter(&endPerformanceCount);
				}
				startPerformanceCount = endPerformanceCount;
			}

		}
	}
	return 0;
} 
