#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windows.h>
#include "app.h"
#include <dsound.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>

#define OPEN_BUTTON 13
#define PLAY_BUTTON 41
#define RESTART_BUTTON 42
#define BACK_BUTTON 43

#define INIT_STATE 90
#define INPUT_STATE 91
#define PLAYING_STATE 92
#define ERROR_STATE 99

static BOOL GlobalRunning;
static HWND PlayButton;
static HWND RestartButton;
static HWND OpenButton;
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

static float SourceSampleRate = 44100.0f;

static char* input = 0;
note* noteStorage = 0;

void FreeNotes(void)
{
    if (phase)
    {
        free(phase);
        phase = NULL;
    }
    if (prevAmp)
    {
        free(prevAmp);
        prevAmp = NULL;
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

void LoadNotes(HWND Window, char* text)
{
    char* start = strstr(text, "\"noteCount\":");
    int parsedNoteCount = 0;
    if (start && sscanf(start, "\"noteCount\": %d", &parsedNoteCount) == 1 && parsedNoteCount > 0)
    {
        SourceSampleRate = 44100.0f;
        char* sampleRateStart = strstr(text, "\"sampleRate\":");
        if (sampleRateStart)
        {
            float parsedSampleRate = 0;
            if (sscanf(sampleRateStart, "\"sampleRate\": %f", &parsedSampleRate) == 1 && parsedSampleRate > 0)
            {
                SourceSampleRate = parsedSampleRate;
            }
        }
        FreeNotes();
        noteStorage = malloc(parsedNoteCount * sizeof(note));
        if (noteStorage)
        {
            memset(noteStorage, 0, parsedNoteCount * sizeof(note));
            int parsedNotes = 0;
            char* cursor = text;
            for (int i = 0; i < parsedNoteCount; i++)
            {
                cursor = strstr(cursor, "\"fundamental\":");
                if (!cursor || sscanf(cursor, "\"fundamental\": %f", &noteStorage[i].fundamental) != 1) break;
                cursor = strstr(cursor, "\"startFrame\":");
                if (!cursor || sscanf(cursor, "\"startFrame\": %d", &noteStorage[i].startFrame) != 1) break;
                cursor = strstr(cursor, "\"endFrame\":");
                if (!cursor || sscanf(cursor, "\"endFrame\": %d", &noteStorage[i].endFrame) != 1) break;
                char* envSection = strstr(cursor, "\"envelope\":");
                cursor = strstr(cursor, "\"envelopeLength\":");
                if (!cursor || sscanf(cursor, "\"envelopeLength\": %d", &noteStorage[i].envelopeLength) != 1) break;
                if (noteStorage[i].envelopeLength > 0)
                {
                    noteStorage[i].envelope = malloc(noteStorage[i].envelopeLength * sizeof(float));
                    if (noteStorage[i].envelope)
                    {
                        memset(noteStorage[i].envelope, 0, noteStorage[i].envelopeLength * sizeof(float));
                        if (envSection)
                        {
                            char* envStart = strchr(envSection, '{');
                            char* envEnd = envStart ? strchr(envStart, '}') : NULL;
                            if (envStart && envEnd)
                            {
                                char* p = envStart + 1;
                                for (int h = 0; h < noteStorage[i].envelopeLength; h++)
                                {
                                    char* colon = strchr(p, ':');
                                    if (!colon || colon > envEnd) break;
                                    char* next = NULL;
                                    float value = strtof(colon + 1, &next);
                                    if (next == colon + 1) break;
                                    noteStorage[i].envelope[h] = value;
                                    p = next;
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
                    noteStorage[i].startTime = (float)noteStorage[i].startFrame / (SourceSampleRate / 1024.0f);
                    noteStorage[i].endTime = (float)noteStorage[i].endFrame / (SourceSampleRate / 1024.0f);
                    if (noteStorage[i].endFrame > maxEndFrame) maxEndFrame = noteStorage[i].endFrame;
                }
                totalTime = (float)maxEndFrame / (SourceSampleRate / 1024.0f);
                if (maxEndFrame == 0)
                {
                    MessageBoxA(Window, "Endframe is 0, if you are confused please see the readme: https://github.com/minye065/Aud/blob/main/README.md ERROR101", "Error 101", MB_OK | MB_ICONERROR);
                }
                else
                {
                    phase = malloc(noteCount * sizeof(float));
                    prevAmp = malloc(noteCount * sizeof(float));
                    if (phase && prevAmp)
                    {
                        memset(phase, 0, noteCount * sizeof(float));
                        memset(prevAmp, 0, noteCount * sizeof(float));
                        CurrentState = PLAYING_STATE;
                        ShowWindow(OpenButton, SW_HIDE);
                        ShowWindow(PlayButton, SW_SHOW);
                        ShowWindow(RestartButton, SW_SHOW);
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

static void FillSamples(int16_t* SampleOut, DWORD SampleCount)
{
    for (DWORD Index = 0; Index < SampleCount; ++Index)
    {
        float mixedAmplitude = 0.0f;
        if (isPlaying && CurrentState == PLAYING_STATE && noteStorage)
        {
            mixedAmplitude = getActiveNotes(currentTime, noteCount, noteStorage);
            currentTime += 1.0f / 44100.0f;
            if (currentTime >= totalTime)
            {
                isPlaying = 0;
                currentTime = 0.0f;
                if (phase)
                {
                    memset(phase, 0, noteCount * sizeof(float));
                }
                if (prevAmp)
                {
                    memset(prevAmp, 0, noteCount * sizeof(float));
                }
                SetWindowTextA(PlayButton, "Start");
            }
        }
        if (mixedAmplitude > 1.0f) mixedAmplitude = 1.0f;
        if (mixedAmplitude < -1.0f) mixedAmplitude = -1.0f;
        *SampleOut++ = (int16_t)(mixedAmplitude * 32767.0f);
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
        MoveWindow(OpenButton, Width / 2 - 60, Height / 2 - 15, 120, 30, TRUE);
        MoveWindow(PlayButton, Width / 2 - 130, (Height / 3) * 2, 120, 30, TRUE);
        MoveWindow(RestartButton, Width / 2 + 10, (Height / 3) * 2, 120, 30, TRUE);
        MoveWindow(BackButton, 40, 10, 120, 30, TRUE);
    } break;
    case WM_COMMAND:
    {
        switch (LOWORD(WParam))
        {
        case OPEN_BUTTON:
        {
            OPENFILENAMEA OpenFileName = { 0 };
            char FilePath[MAX_PATH] = { 0 };
            OpenFileName.lStructSize = sizeof(OPENFILENAMEA);
            OpenFileName.hwndOwner = Window;
            OpenFileName.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
            OpenFileName.lpstrFile = FilePath;
            OpenFileName.nMaxFile = MAX_PATH;
            OpenFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&OpenFileName))
            {
                FILE* file = fopen(FilePath, "rb");
                if (file)
                {
                    fseek(file, 0, SEEK_END);
                    long fileSize = ftell(file);
                    fseek(file, 0, SEEK_SET);
                    if (input)
                    {
                        free(input);
                        input = NULL;
                    }
                    input = malloc(fileSize + 1);
                    if (input)
                    {
                        fread(input, 1, fileSize, file);
                        input[fileSize] = 0;
                        LoadNotes(Window, input);
                    }
                    fclose(file);
                }
            }
        } break;
        case PLAY_BUTTON:
        {
            if (isPlaying)
            {
                isPlaying = 0;
                SetWindowTextA(PlayButton, "Resume");
            }
            else
            {
                if (currentTime >= totalTime && totalTime > 0.0f)
                {
                    currentTime = 0.0f;
                    if (phase)
                    {
                        memset(phase, 0, noteCount * sizeof(float));
                    }
                    if (prevAmp)
                    {
                        memset(prevAmp, 0, noteCount * sizeof(float));
                    }
                }
                isPlaying = 1;
                SetWindowTextA(PlayButton, "Pause");
            }
            InvalidateRect(Window, NULL, FALSE);
        } break;
        case RESTART_BUTTON:
        {
            currentTime = 0.0f;
            if (phase)
            {
                memset(phase, 0, noteCount * sizeof(float));
            }
            if (prevAmp)
            {
                memset(prevAmp, 0, noteCount * sizeof(float));
            }
            isPlaying = 1;
            SetWindowTextA(PlayButton, "Pause");
            InvalidateRect(Window, NULL, FALSE);
        } break;
        case BACK_BUTTON:
        {
            CurrentState = INPUT_STATE;
            isPlaying = 0;
            currentTime = 0.0f;
            if (phase)
            {
                memset(phase, 0, noteCount * sizeof(float));
            }
            if (prevAmp)
            {
                memset(prevAmp, 0, noteCount * sizeof(float));
            }
            SetWindowTextA(PlayButton, "Start");
            ShowWindow(OpenButton, SW_SHOW);
            ShowWindow(PlayButton, SW_HIDE);
            ShowWindow(RestartButton, SW_HIDE);
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

            PlayButton = CreateWindowA("BUTTON", "Start", WS_CHILD,
                Width / 2 - 130, (Height / 3) * 2, 120, 30, Window, (HMENU)PLAY_BUTTON, 0, 0);
            RestartButton = CreateWindowA("BUTTON", "Restart", WS_CHILD,
                Width / 2 + 10, (Height / 3) * 2, 120, 30, Window, (HMENU)RESTART_BUTTON, 0, 0);
            OpenButton = CreateWindowA("BUTTON", "Open", WS_CHILD | WS_VISIBLE,
                Width / 2 - 60, Height / 2 - 15, 120, 30, Window, (HMENU)OPEN_BUTTON, 0, 0);
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
                            FillSamples((int16_t*)Ref1, Size1 / sizeof(int16_t));
                            FillSamples((int16_t*)Ref2, Size2 / sizeof(int16_t));

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