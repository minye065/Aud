#include <stdint.h>
#include <math.h>
#include "app.h"

float* phase;
//	phase = malloc(noteCount * sizeof(float))

int noteOrdering(const void* a, const void* b)
{
	note* noteA = (note*)a;
	note* noteB = (note*)b;
	return noteA->startFrame - noteB->startFrame;
}

float getActiveNotes(float currentTime, char* input, int noteCount, note* noteStorage)
{
	float mixedAmplitude = 0.0f;
	for (int i = 0; i < noteCount; i++)
	{
		float startTime = (float)noteStorage[i].startFrame / 44100.0f;
		float endTime = (float)noteStorage[i].endFrame / 44100.0f;
		if (noteStorage[i].startTime > currentTime) break;
		if (noteStorage[i].endTime < currentTime) continue;
		float timeUntilNote = currentTime - noteStorage[i].startTime;
		float hopDuration = 1024.0f / 44100.0f;
		float position = timeUntilNote / hopDuration;
		float wholeNumberValue = noteStorage[i].envelope[(int)floor(position)];
		float decimalNumberValue = 0;
		if ((floor(position)) + 1 < noteStorage[i].envelopeLength)
		{
			decimalNumberValue = noteStorage[i].envelope[(int)floor(position) + 1];
		}
		float decimalPercent = (position) - floor(position);
		mixedAmplitude += sin(phase[i]) * (decimalNumberValue * decimalPercent) + (wholeNumberValue * (1 - decimalPercent));
		phase[i] += 2 * PI * noteStorage[i].fundamental / 44100.0f;
		if (phase[i] >= 2 * PI) 
		{
			phase[i] -= 2 * PI;
		}
	}
	return(mixedAmplitude);
}