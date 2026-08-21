#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windows.h>
#include "app.h"
#include <dsound.h>
#include <stdint.h>
#include <stdio.h>

#define INPUT_BOX 11
#define DONE_BUTTON 12
#define PLAY_BUTTON 41
#define STOP_BUTTON 42
#define BACK_BUTTON 43

#define INIT_STATE 90
#define INPUT_STATE 91
#define PLAYING_STATE 92

static BOOL GlobalRunning;
static HWND PlayButton;
static HWND StopButton;
static HWND InputBox;
static HWND DoneButton;
static HWND BackButton;

static LPDIRECTSOUNDBUFFER SecondaryBuffer;
static DWORD PlayBufferSize = 44100 * sizeof(int16_t);
static DWORD NextWriteOffset = 0;

static int CurrentState = INIT_STATE;
static int isPlaying;

static float currentTime;
static float totalTime;
static float phase;
static int noteCount;

static char* input;
note* noteStorage;

LRESULT CALLBACK
Win32MainWindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;
    switch (Message)
    {
    case WM_DESTROY:
    {
        GlobalRunning = FALSE;
    } break;
    case WM_CLOSE:
    {
        GlobalRunning = FALSE;
    } break;
    case WM_KEYUP:
    {
    } break;
    case WM_SIZE:
    {
        int Width = LOWORD(LParam);
        int Height = HIWORD(LParam);
        if (CurrentState == INIT_STATE)
        {
            OutputDebugStringA("ERR - WM_SIZE call has state INIT");
        }
        MoveWindow(InputBox, Width / 2 - 200, (Height / 3) - 100, 400, 200, TRUE);
        MoveWindow(DoneButton, Width / 2 - 60, (Height / 3) * 2, 120, 30, TRUE);
        MoveWindow(PlayButton, Width / 2 - 130, (Height / 3) * 2, 120, 30, TRUE);
        MoveWindow(StopButton, Width / 2 + 10, (Height / 3) * 2, 120, 30, TRUE);
        MoveWindow(BackButton, 40, 10, 120, 30, TRUE);
    } break;
    case WM_COMMAND:
    {
        switch (LOWORD(WParam))
        {
        case INPUT_BOX: break;
        case DONE_BUTTON:
        {
            CurrentState = PLAYING_STATE;
            ShowWindow(InputBox, SW_HIDE);
            ShowWindow(DoneButton, SW_HIDE);
            ShowWindow(PlayButton, SW_SHOW);
            ShowWindow(StopButton, SW_SHOW);
            ShowWindow(BackButton, SW_SHOW);
            InvalidateRect(Window, NULL, TRUE);
            input = GetWindowTextA(InputBox, input, 100000000);
            noteCount = sscanf(input, "");
            noteStorage = malloc(noteCount * sizeof(note))

        } break;
        case PLAY_BUTTON:
        {
            isPlaying = 1;
        } break;
        case STOP_BUTTON:
        {
            isPlaying = 0;
            currentTime = 0.0f;
            if (phase && noteCount > 0)
            {
                memset(phase, 0, noteCount * sizeof(float));
            }
            InvalidateRect(Window, NULL, FALSE);
        } break;
        case BACK_BUTTON:
        {
            CurrentState = INPUT_STATE;
            isPlaying = 0;
            currentTime = 0.0f;
            ShowWindow(InputBox, SW_SHOW);
            ShowWindow(DoneButton, SW_SHOW);
            ShowWindow(PlayButton, SW_HIDE);
            ShowWindow(StopButton, SW_HIDE);
            ShowWindow(BackButton, SW_HIDE);
            InvalidateRect(Window, NULL, TRUE);
        } break;
        }
    } break;
    case WM_PAINT:
    {
        PAINTSTRUCT Paint;
        HDC DeviceContext = BeginPaint(Window, &Paint);
        RECT ClientRect;
        GetClientRect(Window, &ClientRect);

        if (CurrentState == PLAYING_STATE)
        {
            int Width = ClientRect.right - ClientRect.left;
            int Height = ClientRect.bottom - ClientRect.top;

            RECT BarRect;
            BarRect.left = Width / 2 - 150;
            BarRect.right = Width / 2 + 150;
            BarRect.top = ((Height / 3) * 2) - 30;
            BarRect.bottom = BarRect.top + 8;

            HBRUSH BgBrush = CreateSolidBrush(RGB(100, 100, 100));
            FillRect(DeviceContext, &BarRect, BgBrush);
            DeleteObject(BgBrush);

            float Ratio = 0.0f;
            if (totalTime > 0.0f)
            {
                Ratio = currentTime / totalTime;
                if (Ratio > 1.0f) Ratio = 1.0f;
                if (Ratio < 0.0f) Ratio = 0.0f;
            }

            RECT FillRectArea = BarRect;
            FillRectArea.right = FillRectArea.left + (int)((BarRect.right - BarRect.left) * Ratio);

            HBRUSH FillBrush = CreateSolidBrush(RGB(0, 255, 128));
            FillRect(DeviceContext, &FillRectArea, FillBrush);
            DeleteObject(FillBrush);
        }

        EndPaint(Window, &Paint);
    } break;

    default:
    {
        Result = DefWindowProc(Window, Message, WParam, LParam);
    } break;
    }

    return (Result);
}

void InitDirectSound(HWND Window)
{
    LPDIRECTSOUND8 DirectSound;
    if (SUCCEEDED(DirectSoundCreate8(NULL, &DirectSound, NULL)))
    {
        if (SUCCEEDED(IDirectSound8_SetCooperativeLevel(DirectSound, Window, DSSCL_PRIORITY)))
        {
            WAVEFORMATEX WaveFormat = { 0 };
            WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
            WaveFormat.nChannels = 1;
            WaveFormat.nSamplesPerSec = 44100;
            WaveFormat.wBitsPerSample = 16;
            WaveFormat.nBlockAlign = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
            WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
            DSBUFFERDESC PrimaryBufferDesc = { 0 };
            PrimaryBufferDesc.dwSize = sizeof(PrimaryBufferDesc);
            PrimaryBufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
            LPDIRECTSOUNDBUFFER PrimaryBuffer;
            if (SUCCEEDED(IDirectSound8_CreateSoundBuffer(DirectSound, &PrimaryBufferDesc, &PrimaryBuffer, NULL)))
            {
                IDirectSoundBuffer_SetFormat(PrimaryBuffer, &WaveFormat);
                IDirectSoundBuffer_Release(PrimaryBuffer);
            }
            DSBUFFERDESC SecondaryBufferDesc = { 0 };
            SecondaryBufferDesc.dwSize = sizeof(SecondaryBufferDesc);
            SecondaryBufferDesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
            SecondaryBufferDesc.dwBufferBytes = PlayBufferSize;
            SecondaryBufferDesc.lpwfxFormat = &WaveFormat;

            if (SUCCEEDED(IDirectSound8_CreateSoundBuffer(DirectSound, &SecondaryBufferDesc, &SecondaryBuffer, NULL)))
            {
                IDirectSoundBuffer_Play(SecondaryBuffer, 0, 0, DSBPLAY_LOOPING);
            }
        }
    }
}

int WINAPI WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
    WNDCLASSA WindowClass = { 0 };
    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = "AudWindowClass";
    WindowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (RegisterClassA(&WindowClass))
    {
        HWND Window = CreateWindowExA(
            0,
            WindowClass.lpszClassName,
            "Aud",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            0,
            0,
            Instance,
            0
        );
        if (Window)
        {
            OutputDebugStringA("window");
            GlobalRunning = TRUE;
            CurrentState = INPUT_STATE;

            InitDirectSound(Window);

            RECT WindowRect;
            GetWindowRect(Window, &WindowRect);
            int Width = WindowRect.right - WindowRect.left;
            int Height = WindowRect.bottom - WindowRect.top;

            PlayButton = CreateWindowA("BUTTON", "Play", WS_CHILD,
                (Width / 2) - 60 - 40, (Height / 3) * 2, 120, 30, Window, (HMENU)PLAY_BUTTON, 0, 0);
            StopButton = CreateWindowA("BUTTON", "Stop", WS_CHILD,
                Width / 2 + 40, (Height / 3) * 2, 120, 30, Window, (HMENU)STOP_BUTTON, 0, 0);
            InputBox = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                Width / 2 - 200, Height / 3 - 150, 400, 300, Window, (HMENU)INPUT_BOX, 0, 0);
            DoneButton = CreateWindowA("BUTTON", "Done", WS_CHILD | WS_VISIBLE,
                Width / 2 - 40, (Height / 3) * 2, 120, 30, Window, (HMENU)DONE_BUTTON, 0, 0);
            BackButton = CreateWindowA("BUTTON", "Back", WS_CHILD,
                40, 10, 80, 20, Window, (HMENU)BACK_BUTTON, 0, 0);

            while (GlobalRunning)
            {
                MSG Message;
                while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
                {
                    if (Message.message == WM_QUIT)
                    {
                        GlobalRunning = FALSE;
                    }
                    TranslateMessage(&Message);
                    DispatchMessage(&Message);
                }
                Sleep(20);

                DWORD PlayCursor = 0;
                DWORD WriteCursor = 0;
                if (SecondaryBuffer && SUCCEEDED(IDirectSoundBuffer_GetCurrentPosition(SecondaryBuffer, &PlayCursor, &WriteCursor)))
                {
                    DWORD BytesToWrite = 0;
                    if (NextWriteOffset != PlayCursor)
                    {
                        if (NextWriteOffset > PlayCursor)
                        {
                            BytesToWrite = (PlayBufferSize - NextWriteOffset) + PlayCursor;
                        }
                        else
                        {
                            BytesToWrite = PlayCursor - NextWriteOffset;
                        }
                    }

                    if (BytesToWrite > 0)
                    {
                        void* Ref1;
                        DWORD Size1;
                        void* Ref2;
                        DWORD Size2;

                        if (SUCCEEDED(IDirectSoundBuffer_Lock(SecondaryBuffer, NextWriteOffset, BytesToWrite, &Ref1, &Size1, &Ref2, &Size2, 0)))
                        {
                            int16_t* SampleOut = (int16_t*)Ref1;
                            DWORD SampleCount1 = Size1 / sizeof(int16_t);
                            for (DWORD Index = 0; Index < SampleCount1; ++Index)
                            {
                                float mixedAmplitude = 0.0f;
                                if (isPlaying && CurrentState == PLAYING_STATE)
                                {
                                    mixedAmplitude = getActiveNotes(currentTime, input);
                                    currentTime += 1.0f / 44100.0f;
                                    if (currentTime >= totalTime)
                                    {
                                        isPlaying = 0;
                                        currentTime = 0.0f;
                                    }
                                }
                                *SampleOut++ = (int16_t)(mixedAmplitude * 32767.0f);
                            }

                            SampleOut = (int16_t*)Ref2;
                            DWORD SampleCount2 = Size2 / sizeof(int16_t);
                            for (DWORD Index = 0; Index < SampleCount2; ++Index)
                            {
                                float mixedAmplitude = 0.0f;
                                if (isPlaying && CurrentState == PLAYING_STATE)
                                {
                                    mixedAmplitude = getActiveNotes(currentTime, input);
                                    currentTime += 1.0f / 44100.0f;
                                    if (currentTime >= totalTime)
                                    {
                                        isPlaying = 0;
                                        currentTime = 0.0f;
                                    }
                                }
                                *SampleOut++ = (int16_t)(mixedAmplitude * 32767.0f);
                            }

                            IDirectSoundBuffer_Unlock(SecondaryBuffer, Ref1, Size1, Ref2, Size2);
                            NextWriteOffset = (NextWriteOffset + BytesToWrite) % PlayBufferSize;

                            if (CurrentState == PLAYING_STATE)
                            {
                                InvalidateRect(Window, NULL, FALSE);
                            }
                        }
                    }
                }
            }
        }
    }

    return (0);
}