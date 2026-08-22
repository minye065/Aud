#include <stdint.h>
#include <math.h>
#include "app.h"

float* phase;

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
		if (noteStorage[i].startTime > currentTime) break;
		if (noteStorage[i].endTime < currentTime) continue;
		if (!noteStorage[i].envelope) continue;
		float timeUntilNote = currentTime - noteStorage[i].startTime;
		float hopDuration = 1024.0f / 44100.0f;
		float position = timeUntilNote / hopDuration;
		float wholePosition = floor(position);
		int envelopeIndex = (int)wholePosition;
		if (envelopeIndex < 0) envelopeIndex = 0;
		if (envelopeIndex >= noteStorage[i].envelopeLength) envelopeIndex = noteStorage[i].envelopeLength - 1;
		float wholeNumberValue = noteStorage[i].envelope[envelopeIndex];
		float decimalNumberValue = 0;
		if (envelopeIndex + 1 < noteStorage[i].envelopeLength)
		{
			decimalNumberValue = noteStorage[i].envelope[envelopeIndex + 1];
		}
		float decimalPercent = position - wholePosition;
		mixedAmplitude += sin(phase[i]) * (decimalNumberValue * decimalPercent) + (wholeNumberValue * (1 - decimalPercent));
		phase[i] += 2 * PI * noteStorage[i].fundamental / 44100.0f;
		if (phase[i] >= 2 * PI)
		{
			phase[i] -= 2 * PI;
		}
	}
	return(mixedAmplitude);
}
