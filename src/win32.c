#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windows.h>
#include "app.h"
#include <dsound.h>
#include <stdint.h>
#include <stdio.h>

#define INPUT_BOX 11
#define DONE_BUTTON 12
#define PLAY_BUTTON 41
#define BLANK_BUTTON 42
#define BACK_BUTTON 43

#define INIT_STATE 90
#define INPUT_STATE 91
#define PLAYING_STATE 92
#define ERROR_STATE 99

static BOOL GlobalRunning;
static HWND PlayButton;
static HWND BlankButton;
static HWND InputBox;
static HWND DoneButton;
static HWND BackButton;

static LPDIRECTSOUNDBUFFER SecondaryBuffer;
static DWORD PlayBufferSize = 44100 * sizeof(int16_t);
static DWORD SoundLatencyBytes = 50 * 44100 * sizeof(int16_t) / 1000;
static DWORD NextWriteOffset = 0;

static int CurrentState = INIT_STATE;
static int isPlaying;

static float currentTime;
static float totalTime;
static int noteCount;

static char* input = 0;
note* noteStorage = 0;

void FreeNotes(void)
{
    if (phase)
    {
        free(phase);
        phase = NULL;
    }
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
        MoveWindow(BlankButton, Width / 2 + 10, (Height / 3) * 2, 120, 30, TRUE);
        MoveWindow(BackButton, 40, 10, 120, 30, TRUE);
    } break;
    case WM_COMMAND:
    {
        switch (LOWORD(WParam))
        {
        case INPUT_BOX: break;
        case DONE_BUTTON:
        {
            if (input)
            {
                free(input);
                input = NULL;
            }
            int textLength = GetWindowTextLength(InputBox);
            input = malloc(textLength + 1);
            if (input)
            {
                GetWindowTextA(InputBox, input, textLength + 1);
                char* start = strstr(input, "\"noteCount\":");
                int parsedNoteCount = 0;
                if (start && sscanf(start, "\"noteCount\": %d", &parsedNoteCount) == 1 && parsedNoteCount > 0)
                {
                    FreeNotes();
                    noteStorage = malloc(parsedNoteCount * sizeof(note));
                    if (noteStorage)
                    {
                        memset(noteStorage, 0, parsedNoteCount * sizeof(note));
                        int parsedNotes = 0;
                        char* cursor = input;
                        for (int i = 0; i < parsedNoteCount; i++)
                        {
                            cursor = strstr(cursor, "\"fundamental\":");
                            if (!cursor || sscanf(cursor, "\"fundamental\": %f", &noteStorage[i].fundamental) != 1) break;
                            cursor = strstr(cursor, "\"startFrame\":");
                            if (!cursor || sscanf(cursor, "\"startFrame\": %d", &noteStorage[i].startFrame) != 1) break;
                            cursor = strstr(cursor, "\"endFrame\":");
                            if (!cursor || sscanf(cursor, "\"endFrame\": %d", &noteStorage[i].endFrame) != 1) break;
                            cursor = strstr(cursor, "\"envelopeLength\":");
                            if (!cursor || sscanf(cursor, "\"envelopeLength\": %d", &noteStorage[i].envelopeLength) != 1) break;
                            if (noteStorage[i].envelopeLength > 0)
                            {
                                noteStorage[i].envelope = malloc(noteStorage[i].envelopeLength * sizeof(float));
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
                            }
                            cursor = strstr(cursor, "}");
                            if (!cursor) break;
                            parsedNotes = i + 1;
                        }
                        if (parsedNotes > 0)
                        {
                            noteCount = parsedNotes;
                            qsort(noteStorage, noteCount, sizeof(note), noteOrdering);
                            int maxEndFrame = 0;
                            for (int i = 0; i < noteCount; i++)
                            {
                                noteStorage[i].startTime = (float)noteStorage[i].startFrame / (44100.0f / 1024.0f);
                                noteStorage[i].endTime = (float)noteStorage[i].endFrame / (44100.0f / 1024.0f);
                                if (noteStorage[i].endFrame > maxEndFrame) maxEndFrame = noteStorage[i].endFrame;
                            }
                            totalTime = (float)maxEndFrame / (44100.0f / 1024.0f);
                            if (maxEndFrame == 0)
                            {
                                MessageBoxA(Window, "Endframe is 0, if you are confused please see the readme: https://github.com/minye065/Aud/blob/main/README.md ERROR101", "Error 101", MB_OK | MB_ICONERROR);
                            }
                            else
                            {
                                phase = malloc(noteCount * sizeof(float));
                                if (phase)
                                {
                                    memset(phase, 0, noteCount * sizeof(float));
                                    CurrentState = PLAYING_STATE;
                                    ShowWindow(InputBox, SW_HIDE);
                                    ShowWindow(DoneButton, SW_HIDE);
                                    ShowWindow(PlayButton, SW_SHOW);
                                    ShowWindow(BlankButton, SW_SHOW);
                                    ShowWindow(BackButton, SW_SHOW);
                                    InvalidateRect(Window, NULL, TRUE);
                                }
                                else
                                {
                                    FreeNotes();
                                    MessageBoxA(Window, "Out of memory", "Error", MB_OK | MB_ICONERROR);
                                }
                            }
                        }
                        else
                        {
                            FreeNotes();
                            MessageBoxA(Window, "Not usable data, if you are confused please see the readme: https://github.com/minye065/Aud/blob/main/README.md ERROR100", "Error 100", MB_OK | MB_ICONERROR);
                        }
                    }
                    else
                    {
                        MessageBoxA(Window, "Out of memory", "Error", MB_OK | MB_ICONERROR);
                    }
                }
                else
                {
                    MessageBoxA(Window, "Not usable data, if you are confused please see the readme: https://github.com/minye065/Aud/blob/main/README.md ERROR100", "Error 100", MB_OK | MB_ICONERROR);
                }
            }
        } break;
        case PLAY_BUTTON:
        {
            if (isPlaying)
            {
                isPlaying = 0;
                currentTime = 0.0f;
                if (phase)
                {
                    memset(phase, 0, noteCount * sizeof(float));
                }
                SetWindowTextA(PlayButton, "Play");
            }
            else
            {
                isPlaying = 1;
                SetWindowTextA(PlayButton, "Stop");
            }
            InvalidateRect(Window, NULL, FALSE);
        } break;
        case BLANK_BUTTON: break;
        case BACK_BUTTON:
        {
            CurrentState = INPUT_STATE;
            isPlaying = 0;
            currentTime = 0.0f;
            if (phase)
            {
                memset(phase, 0, noteCount * sizeof(float));
            }
            SetWindowTextA(PlayButton, "Play");
            ShowWindow(InputBox, SW_SHOW);
            ShowWindow(DoneButton, SW_SHOW);
            ShowWindow(PlayButton, SW_HIDE);
            ShowWindow(BlankButton, SW_HIDE);
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

            RECT ClientRect;
            GetClientRect(Window, &ClientRect);
            int Width = ClientRect.right - ClientRect.left;
            int Height = ClientRect.bottom - ClientRect.top;

            PlayButton = CreateWindowA("BUTTON", "Play", WS_CHILD,
                Width / 2 - 130, (Height / 3) * 2, 120, 30, Window, (HMENU)PLAY_BUTTON, 0, 0);
            BlankButton = CreateWindowA("BUTTON", "", WS_CHILD,
                Width / 2 + 10, (Height / 3) * 2, 120, 30, Window, (HMENU)BLANK_BUTTON, 0, 0);
            InputBox = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                Width / 2 - 200, (Height / 3) - 100, 400, 200, Window, (HMENU)INPUT_BOX, 0, 0);
            SendMessageA(InputBox, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
            DoneButton = CreateWindowA("BUTTON", "Done", WS_CHILD | WS_VISIBLE,
                Width / 2 - 60, (Height / 3) * 2, 120, 30, Window, (HMENU)DONE_BUTTON, 0, 0);
            BackButton = CreateWindowA("BUTTON", "Back", WS_CHILD,
                40, 10, 120, 30, Window, (HMENU)BACK_BUTTON, 0, 0);

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
                    DWORD TargetOffset = (PlayCursor + SoundLatencyBytes) % PlayBufferSize;
                    DWORD BytesToWrite = 0;
                    if (TargetOffset != NextWriteOffset)
                    {
                        if (TargetOffset > NextWriteOffset)
                        {
                            BytesToWrite = TargetOffset - NextWriteOffset;
                        }
                        else
                        {
                            BytesToWrite = (PlayBufferSize - NextWriteOffset) + TargetOffset;
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
                                        if (phase)
                                        {
                                            memset(phase, 0, noteCount * sizeof(float));
                                        }
                                        SetWindowTextA(PlayButton, "Play");
                                    }
                                }
                                if (mixedAmplitude > 1.0f) mixedAmplitude = 1.0f;
                                if (mixedAmplitude < -1.0f) mixedAmplitude = -1.0f;
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
                                        if (phase)
                                        {
                                            memset(phase, 0, noteCount * sizeof(float));
                                        }
                                        SetWindowTextA(PlayButton, "Play");
                                    }
                                }
                                if (mixedAmplitude > 1.0f) mixedAmplitude = 1.0f;
                                if (mixedAmplitude < -1.0f) mixedAmplitude = -1.0f;
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