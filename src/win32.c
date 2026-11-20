#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windows.h>
#include "app.h"
#include <dsound.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

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

static LPDIRECTSOUND8 DirectSound;
static LPDIRECTSOUNDBUFFER SecondaryBuffer;
static DWORD PlayBufferSize = 44100 * sizeof(int16_t);
static DWORD SoundLatencyBytes = 50 * 44100 * sizeof(int16_t) / 1000;
static DWORD NextWriteOffset = 0;

static int currentState = INIT_STATE;
static int isPlaying;

static float currentTime;
static float totalTime;
static int64_t playbackSample;

static char* input = 0;
static song currentSong;

static HBRUSH progressBarPlayedBrush;
static HBRUSH progressBarRemainingBrush;
static int lastProgressBarFillWidth = -1;

void freeNotes(void)
{
    free(currentSong.notes);
    memset(&currentSong, 0, sizeof(currentSong));
}

void InitDirectSound(HWND Window, int sampleRate);

void loadNotes(HWND Window, char* text)
{
    freeNotes();
    char* itemName;
    item newItem;
    int lineNumber = 0;
    int firstLine = 0;
    const char* errorText = NULL;
    char message[160];
    char warnings[512];
    size_t warningLength = 0;

    char* lineStart = text;
    while (*lineStart)
    {
        char* lineEnd = lineStart;
        while (*lineEnd && *lineEnd != '\n') lineEnd++;
        char* nextLine = *lineEnd ? lineEnd + 1 : lineEnd;
        *lineEnd = 0;
        char* line = lineStart;
        lineStart = nextLine;
        lineNumber++;
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t' || line[length - 1] == '\r')) line[--length] = 0;
        if (length == 0) continue;
        char* word = strtok(line, " \t");
        if (!firstLine)
        {
            if (strcmp(word, "AUD") != 0)
            {
                errorText = "first line must be AUD songName sampleRate hop";
                goto fail;
            }
            char* songNameWord = strtok(NULL, " \t");
            char* sampleRateWord = strtok(NULL, " \t");
            char* hopWord = strtok(NULL, " \t");
            if (!songNameWord || !sampleRateWord || !hopWord)
            {
                errorText = "missing songName, sampleRate, hop";
                goto fail;
            }
            if (strlen(songNameWord) >= sizeof(currentSong.name))
            {
                errorText = "songName too long";
                goto fail;
            }
            memcpy(currentSong.name, songNameWord, strlen(songNameWord) + 1);
            char* sampleRateEnd;
            char* hopEnd;
            long parsedSampleRate = strtol(sampleRateWord, &sampleRateEnd, 10);
            long parsedHop = strtol(hopWord, &hopEnd, 10);
            if (*sampleRateEnd != 0 || *hopEnd != 0 || parsedSampleRate <= 0 || parsedHop <= 0)
            {
                errorText = "sampleRate, hop must be whole numbers > 0";
                goto fail;
            }
            currentSong.sampleRate = (int)parsedSampleRate;
            currentSong.hop = (int)parsedHop;
            firstLine = 1;
        }
        else if (strcmp(word, "item") == 0)
        {
            itemName = strtok(NULL, " \t");
            if (!itemName)
            {
                errorText = "itemName missing";
                goto fail;
            }
            if (strlen(itemName) >= sizeof(newItem.name))
            {
                errorText = "itemName too long";
                goto fail;
            }
            if (currentSong.itemCount >= (int)(sizeof(currentSong.items) / sizeof(currentSong.items[0])))
            {
                errorText = "too many items (max 32)";
                goto fail;
            }
            for (int i = 0; i < currentSong.itemCount; i++)
            {
                if (strcmp(currentSong.items[i].name, itemName) == 0)
                {
                    errorText = "duplicate itemName";
                    goto fail;
                }
            }
            memset(&newItem, 0, sizeof(newItem));
            memcpy(newItem.name, itemName, strlen(itemName) + 1);
            newItem.isTonal = true;

            char* refWord = strtok(NULL, " \t");
            if (!refWord)
            {
                errorText = "item missing ref";
                goto fail;
            }
            char* refEnd;
            newItem.startingPitch = strtof(refWord, &refEnd);
            if (refEnd == refWord || *refEnd != 0 || newItem.startingPitch <= 0)
            {
                errorText = "ref must > 0";
                goto fail;
            }

            char* harmonicsWord = strtok(NULL, " \t");
            if (!harmonicsWord)
            {
                errorText = "item missing harmoincs";
                goto fail;
            }
            char* harmonicsCursor = harmonicsWord;
            int harmonicsCount = 0;
            newItem.attack = 0.01f;
            newItem.decay = 0.1f;
            newItem.sustain = 1.0f;
            newItem.release = 0.1f;
            while (1)
            {
                char* harmonicEnd;
                float parsedHarmonicAmplitude = strtof(harmonicsCursor, &harmonicEnd);
                if (harmonicEnd == harmonicsCursor)
                {
                    errorText = "harmonics not formatted correctly";
                    goto fail;
                }
                if (harmonicsCount >= 16)
                {
                    errorText = "harmonics <= 16 values";
                    goto fail;
                }
                newItem.harmonics[harmonicsCount] = parsedHarmonicAmplitude;
                harmonicsCount++;
                if (*harmonicEnd == 0) break;
                if (*harmonicEnd != ',')
                {
                    errorText = "harmonics not seperated by comma";
                    goto fail;
                }
                harmonicsCursor = harmonicEnd + 1;
            }
            float harmonicsTotal = 0;
            for(int i = 0; i < harmonicsCount - 1; i++)
            {
                harmonicsTotal += newItem.harmonics[i];
                if(harmonicsTotal <= 0) 
                {
                    errorText = "harmonics must add up to be greater then 0";
                    goto fail;
                }
                newItem.harmonics[i] = newItem.harmonics[i] / harmonicsTotal;
            }

            newItem.harmonicsCount = harmonicsCount;

            char* option;
            while ((option = strtok(NULL, " \t")))
            {
                const char* warningText = NULL;
                char* value = strchr(option, ':');
                if (!value) warningText = "an option value has been ignored (text is null)";
                else
                {
                    *value = 0;
                    value++;
                    if (strcmp(option, "adsr") == 0)
                    {
                        float parsedAttack, parsedDecay, parsedSustain, parsedRelease;
                        int adsrCharsRead = 0;
                        if (sscanf(value, "%f,%f,%f,%f%n", &parsedAttack, &parsedDecay, &parsedSustain, &parsedRelease, &adsrCharsRead) != 4 || value[adsrCharsRead] != 0) warningText = "adsr must be 4 comma-separated numbers (ignored)";
                        else if (parsedAttack < 0 || parsedDecay < 0 || parsedRelease < 0 || parsedSustain < 0 || parsedSustain > 1) warningText = "adsr needs a,d,r >= 0 and 0 <= s <= 1 (ignored)";
                        else
                        {
                            newItem.attack = parsedAttack;
                            newItem.decay = parsedDecay;
                            newItem.sustain = parsedSustain;
                            newItem.release = parsedRelease;
                        }
                    }
                    else if (strcmp(option, "kind") == 0)
                    {
                        if (strcmp(value, "noise") == 0) newItem.isTonal = false;
                        else if (strcmp(value, "tonal") == 0) newItem.isTonal = true;
                        else warningText = "kind must be tonal or noise (ignored)";
                    }
                    else warningText = "unknown option, ignored";
                }
                if (warningText && warningLength < sizeof(warnings))
                {
                    warningLength += snprintf(warnings + warningLength, sizeof(warnings) - warningLength, "Line %d: %s\n", lineNumber, warningText);
                }
            }
            currentSong.items[currentSong.itemCount++] = newItem;
        }
        else if (strcmp(word, "use") == 0)
        {
            char* targetItemNameWord = strtok(NULL, " \t");
            if (targetItemNameWord == NULL)
            {
                errorText = "use missing item name";
                goto fail;
            }
            int targetIndexPos = -1;
            for (int i = 0; i < currentSong.itemCount; i++)
            {
                if (strcmp(currentSong.items[i].name, targetItemNameWord) == 0)
                {
                    targetIndexPos = i;
                    break;
                }
            }
            if (targetIndexPos == -1)
            {
                errorText = "use names item that does not exist";
                goto fail;
            }
            if (!currentSong.items[targetIndexPos].isTonal) continue;

            note parsedUseNote;
            memset(&parsedUseNote, 0, sizeof(parsedUseNote));
            parsedUseNote.itemIdentifier = targetIndexPos;
            parsedUseNote.tone = 0.0f;
            parsedUseNote.gain = 1.0f;
            parsedUseNote.startFrame = -1;
            parsedUseNote.endFrame = -1;

            char* useOptionWord;
            while ((useOptionWord = strtok(NULL, " \t")))
            {
                const char* warningText = NULL;
                char* useOptionValue = strchr(useOptionWord, ':');
                if (!useOptionValue) warningText = "a use option has no ':' (ignored)";
                else
                {
                    *useOptionValue = 0;
                    useOptionValue++;
                    if (strcmp(useOptionWord, "tone") == 0)
                    {
                        char* toneCentsEnd;
                        long parsedToneCents = strtol(useOptionValue, &toneCentsEnd, 10);
                        if (toneCentsEnd == useOptionValue || *toneCentsEnd != 0) warningText = "tone must be whole cents (ignored)";
                        else parsedUseNote.tone = (float)parsedToneCents;
                    }
                    else if (strcmp(useOptionWord, "frame") == 0)
                    {
                        char* startFrameEnd;
                        long parsedStartFrame = strtol(useOptionValue, &startFrameEnd, 10);
                        if (startFrameEnd == useOptionValue || *startFrameEnd != '-')
                        {
                            errorText = "frame must be start-end";
                            goto fail;
                        }
                        char* endFrameText = startFrameEnd + 1;
                        char* endFrameEnd;
                        long parsedEndFrame = strtol(endFrameText, &endFrameEnd, 10);
                        if (endFrameEnd == endFrameText || *endFrameEnd != 0)
                        {
                            errorText = "frame must be start-end";
                            goto fail;
                        }
                        if (parsedStartFrame < 0 || parsedEndFrame <= parsedStartFrame)
                        {
                            errorText = "frame needs start >= 0 and end > start";
                            goto fail;
                        }
                        parsedUseNote.startFrame = (int)parsedStartFrame;
                        parsedUseNote.endFrame = (int)parsedEndFrame;
                    }
                    else if (strcmp(useOptionWord, "gain") == 0)
                    {
                        char* gainEnd;
                        float parsedGain = strtof(useOptionValue, &gainEnd);
                        if (gainEnd == useOptionValue || *gainEnd != 0 || parsedGain < 0) warningText = "gain must be a number >= 0 (ignored)";
                        else parsedUseNote.gain = parsedGain;
                    }
                    else warningText = "unknown use option, ignored";
                }
                if (warningText && warningLength < sizeof(warnings))
                {
                    warningLength += snprintf(warnings + warningLength, sizeof(warnings) - warningLength, "Line %d: %s\n", lineNumber, warningText);
                }
            }
            if (parsedUseNote.startFrame == -1)
            {
                errorText = "use missing frame";
                goto fail;
            }

            if (currentSong.noteCount == currentSong.noteCapacity)
            {
                int currentlyReserveredNoteMem = currentSong.noteCapacity ? currentSong.noteCapacity * 2 : 64;
                note* reallocatedSongNotes = realloc(currentSong.notes, currentlyReserveredNoteMem * sizeof(note));
                if(!reallocatedSongNotes)
                {
                    errorText = "out of memory";
                    goto fail;
                }
                currentSong.notes = reallocatedSongNotes;
                currentSong.noteCapacity = currentlyReserveredNoteMem;
            }
            currentSong.notes[currentSong.noteCount++] = parsedUseNote;
        }
        else
        {
            errorText = "unknown line type (expected item or use)";
            goto fail;
        }
    }
    if (!firstLine)
    {
        errorText = "empty file or missing AUD header";
        goto fail;
    }
    if (currentSong.noteCount == 0)
    {
        errorText = "song has no playable use lines";
        goto fail;
    }
    qsort(currentSong.notes, currentSong.noteCount, sizeof(note), noteOrdering);
    currentState = PLAYING_STATE;
    totalTime = 0.0f;
    for(int i = 0; i < currentSong.noteCount; i++)
    {
        item* itemOfTargetNote = &currentSong.items[currentSong.notes[i].itemIdentifier];
        float thisTotalTime = (float)currentSong.notes[i].endFrame * currentSong.hop / currentSong.sampleRate + itemOfTargetNote->release;
        if(thisTotalTime > totalTime) totalTime = thisTotalTime;

    }
    playbackSample = 0;
    currentTime = 0.0f;
    isPlaying = 0;
    lastProgressBarFillWidth = -1;
    SetWindowTextA(PlayButton, "Start");
    InitDirectSound(Window, currentSong.sampleRate);
    ShowWindow(PlayButton, SW_SHOW);
    ShowWindow(RestartButton, SW_SHOW);
    ShowWindow(BackButton, SW_SHOW);
    ShowWindow(OpenButton, SW_HIDE);
    if (warningLength > 0) MessageBoxA(Window, warnings, "Warning", MB_OK | MB_ICONWARNING);
    return;

    fail:
    freeNotes();
    snprintf(message, sizeof(message), "Line %d: %s", lineNumber, errorText);
    MessageBoxA(Window, message, "Error", MB_OK | MB_ICONERROR);
}

static void fillSamples(int16_t* sampleOut, DWORD sampleCount, int64_t startSample)
{
    if(!isPlaying)
    {
        memset(sampleOut, 0, sampleCount * sizeof(int16_t));
        return;
    }
    getActiveNotes(&currentSong, sampleOut, sampleCount, startSample);
}

static RECT getProgressBarRect(HWND Window)
{
    RECT clientRect;
    GetClientRect(Window, &clientRect);
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    RECT progressBarRect;
    progressBarRect.left = clientWidth / 2 - 150;
    progressBarRect.right = clientWidth / 2 + 150;
    progressBarRect.top = ((clientHeight / 3) * 2) - 30;
    progressBarRect.bottom = progressBarRect.top + 8;
    return progressBarRect;
}

static int getProgressBarFillWidth(RECT progressBarRect)
{
    float playedRatio = totalTime > 0.0f ? currentTime / totalTime : 0.0f;
    if (playedRatio > 1.0f) playedRatio = 1.0f;
    if (playedRatio < 0.0f) playedRatio = 0.0f;
    return (int)((progressBarRect.right - progressBarRect.left) * playedRatio);
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
        if(!OpenButton || !PlayButton || !RestartButton || !BackButton) break;
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
                    input = fileSize > 0 ? malloc(fileSize + 1): NULL;
                    if (input)
                    {
                        size_t bytesRead = fread(input, 1, fileSize, file);
                        input[bytesRead] = 0;
                        loadNotes(Window, input);
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
                    playbackSample = 0;
                }
                isPlaying = 1;
                SetWindowTextA(PlayButton, "Pause");
            }
            InvalidateRect(Window, NULL, FALSE);
        } break;
        case RESTART_BUTTON:
        {
            currentTime = 0.0f;
            playbackSample = 0;
            isPlaying = 1;
            SetWindowTextA(PlayButton, "Pause");
            InvalidateRect(Window, NULL, FALSE);
        } break;
        case BACK_BUTTON:
        {
            currentState = INPUT_STATE;
            isPlaying = 0;
            currentTime = 0.0f;
            playbackSample = 0;
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

        if (currentState == PLAYING_STATE)
        {
            RECT progressBarRect = getProgressBarRect(Window);
            int progressBarFillWidth = getProgressBarFillWidth(progressBarRect);

            RECT playedRect = progressBarRect;
            playedRect.right = progressBarRect.left + progressBarFillWidth;
            RECT remainingRect = progressBarRect;
            remainingRect.left = playedRect.right;

            FillRect(DeviceContext, &playedRect, progressBarPlayedBrush);
            FillRect(DeviceContext, &remainingRect, progressBarRemainingBrush);
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

void InitDirectSound(HWND Window, int sampleRate)
{
    if (!DirectSound)
    {
        if (FAILED(DirectSoundCreate8(NULL, &DirectSound, NULL))) return;
        if (FAILED(IDirectSound8_SetCooperativeLevel(DirectSound, Window, DSSCL_PRIORITY)))
        {
            IDirectSound8_Release(DirectSound);
            DirectSound = NULL;
            return;
        }
    }
    if (SecondaryBuffer)
    {
        IDirectSoundBuffer_Stop(SecondaryBuffer);
        IDirectSoundBuffer_Release(SecondaryBuffer);
        SecondaryBuffer = NULL;
    }
    PlayBufferSize = sampleRate * sizeof(int16_t);
    SoundLatencyBytes = 80 * sampleRate * sizeof(int16_t) / 1000;
    NextWriteOffset = 0;

    WAVEFORMATEX WaveFormat = { 0 };
    WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
    WaveFormat.nChannels = 1;
    WaveFormat.nSamplesPerSec = sampleRate;
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
            WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
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
            currentState = INPUT_STATE;

            InitDirectSound(Window, 44100);
            progressBarPlayedBrush = CreateSolidBrush(RGB(0, 255, 128));
            progressBarRemainingBrush = CreateSolidBrush(RGB(100, 100, 100));

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
                    DWORD playToNextWrite = (NextWriteOffset + PlayBufferSize - PlayCursor) % PlayBufferSize;
                    DWORD playToWriteCursor = (WriteCursor + PlayBufferSize - PlayCursor) % PlayBufferSize;
                    DWORD targetOffsetFromPlayCursor = playToWriteCursor + SoundLatencyBytes;
                    if (playToNextWrite < playToWriteCursor)
                    {
                        NextWriteOffset = WriteCursor;
                        playToNextWrite = playToWriteCursor;
                    }
                    DWORD BytesToWrite = playToNextWrite < targetOffsetFromPlayCursor ? targetOffsetFromPlayCursor - playToNextWrite : 0;
                    if (BytesToWrite > 0)
                    {
                        void* Ref1;
                        DWORD Size1;
                        void* Ref2;
                        DWORD Size2;

                        if (SUCCEEDED(IDirectSoundBuffer_Lock(SecondaryBuffer, NextWriteOffset, BytesToWrite, &Ref1, &Size1, &Ref2, &Size2, 0)))
                        {
                            fillSamples((int16_t*)Ref1, Size1 / sizeof(int16_t), playbackSample);
                            if (isPlaying) playbackSample += Size1 / sizeof(int16_t);
                            fillSamples((int16_t*)Ref2, Size2 / sizeof(int16_t), playbackSample);
                            if (isPlaying) playbackSample += Size2 / sizeof(int16_t);
                            currentTime = (float)playbackSample / currentSong.sampleRate;

                            IDirectSoundBuffer_Unlock(SecondaryBuffer, Ref1, Size1, Ref2, Size2);
                            NextWriteOffset = (NextWriteOffset + BytesToWrite) % PlayBufferSize;

                            if (currentState == PLAYING_STATE && isPlaying)
                            {
                                RECT progressBarRect = getProgressBarRect(Window);
                                int progressBarFillWidth = getProgressBarFillWidth(progressBarRect);
                                if (progressBarFillWidth != lastProgressBarFillWidth)
                                {
                                    lastProgressBarFillWidth = progressBarFillWidth;
                                    InvalidateRect(Window, &progressBarRect, FALSE);
                                }
                            }
                        }
                    }
                }
                Sleep(10);
            }
        }
    }

    freeNotes();
    if (input) free(input);

    return (0);
}