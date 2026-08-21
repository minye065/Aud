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
#define ERROR_STATE 99

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

static char* input = 0;
note* noteStorage = 0;

void FreeNotes(void)
{
    if (noteStorage)
    {
        for (int i = 0; i < noteCount; i++)
        {
            if (noteStorage[i].envelope)
            {
                free(noteStorage[i].envelope);
                noteStorage[i].envelope = NULL;
            }
        }
        free(noteStorage);
        noteStorage = NULL;
    }
}

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
    case WM_SIZE:
    {
        int Width = LOWORD(LParam);
        int Height = HIWORD(LParam);
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
        case DONE_BUTTON:
        {
            if (input)
            {
                free(input);
                input = NULL;
            }
            int textLength = GetWindowTextLength(InputBox);
            if (textLength <= 0) break;

            input = (char*)malloc(textLength + 1);
            if (input)
            {
                GetWindowTextA(InputBox, input, textLength + 1);
                char* start = strstr(input, "\"noteCount\":");

                if (start && sscanf(start, "\"noteCount\": %d", &noteCount) == 1 && noteCount > 0)
                {
                    FreeNotes();
                    noteStorage = (note*)malloc(noteCount * sizeof(note));

                    if (noteStorage)
                    {
                        memset(noteStorage, 0, noteCount * sizeof(note));
                        char* cursor = input;
                        BOOL parseError = FALSE;

                        for (int i = 0; i < noteCount; i++)
                        {
                            cursor = strstr(cursor, "\"fundamental\":");
                            if (cursor) sscanf(cursor, "\"fundamental\": %f", &noteStorage[i].fundamental);

                            cursor = strstr(cursor, "\"startFrame\":");
                            if (cursor) sscanf(cursor, "\"startFrame\": %d", &noteStorage[i].startFrame);

                            cursor = strstr(cursor, "\"endFrame\":");
                            if (cursor) sscanf(cursor, "\"endFrame\": %d", &noteStorage[i].endFrame);

                            cursor = strstr(cursor, "\"envelopeLength\":");
                            if (cursor) sscanf(cursor, "\"envelopeLength\": %d", &noteStorage[i].envelopeLength);

                            if (noteStorage[i].envelopeLength > 0 && noteStorage[i].envelopeLength < 100000)
                            {
                                noteStorage[i].envelope = (float*)malloc(noteStorage[i].envelopeLength * sizeof(float));
                                if (noteStorage[i].envelope)
                                {
                                    memset(noteStorage[i].envelope, 0, noteStorage[i].envelopeLength * sizeof(float));
                                    char* envSection = strstr(cursor, "\"envelope\":");
                                    if (envSection)
                                    {
                                        for (int h = 0; h < noteStorage[i].envelopeLength; h++)
                                        {
                                            char searchStr[32];
                                            sprintf(searchStr, "\"%d\":", h);
                                            char* envCursor = strstr(envSection, searchStr);
                                            if (envCursor)
                                            {
                                                char scanFormat[32];
                                                sprintf(scanFormat, "\"%d\": %%f", h);
                                                sscanf(envCursor, scanFormat, &noteStorage[i].envelope[h]);
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    parseError = TRUE;
                                    break;
                                }
                            }

                            cursor = strstr(cursor, "}");
                            if (!cursor) break;
                        }

                        if (!parseError && noteStorage[noteCount - 1].endFrame > 0)
                        {
                            totalTime = (float)noteStorage[noteCount - 1].endFrame / (44100.0f / 1024.0f);
                            CurrentState = PLAYING_STATE;
                            ShowWindow(InputBox, SW_HIDE);
                            ShowWindow(DoneButton, SW_HIDE);
                            ShowWindow(PlayButton, SW_SHOW);
                            ShowWindow(StopButton, SW_SHOW);
                            ShowWindow(BackButton, SW_SHOW);
                            InvalidateRect(Window, NULL, TRUE);
                        }
                        else
                        {
                            FreeNotes();
                            MessageBoxA(Window, "Invalid endFrame or payload data.", "Error 101", MB_OK | MB_ICONERROR);
                        }
                    }
                }
                else
                {
                    MessageBoxA(Window, "Not usable data, if you are confused please see the readme.", "Error 100", MB_OK | MB_ICONERROR);
                }
            }
        } break;
        case PLAY_BUTTON:
        {
            isPlaying = 1;
        } break;
        case STOP_BUTTON:
        {
            isPlaying = 0;
            currentTime = 0.0f;
            phase = 0.0f;
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
            if (totalTime > 0.001f)
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
                                if (isPlaying && CurrentState == PLAYING_STATE && noteStorage)
                                {
                                    mixedAmplitude = getActiveNotes(currentTime, input, noteCount, noteStorage);
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
                                if (isPlaying && CurrentState == PLAYING_STATE && noteStorage)
                                {
                                    mixedAmplitude = getActiveNotes(currentTime, input, noteCount, noteStorage);
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

                            if (CurrentState == PLAYING_STATE && isPlaying)
                            {
                                InvalidateRect(Window, NULL, FALSE);
                            }
                        }
                    }
                }
                Sleep(10);
            }
        }
    }

    FreeNotes();
    if (input) free(input);

    return (0);
}