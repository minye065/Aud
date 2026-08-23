#include <stdint.h>
#include <math.h>
#include "app.h"

float* phase;
float* prevAmp;

int noteOrdering(const void* a, const void* b)
{
	note* noteA = (note*)a;
	note* noteB = (note*)b;
	return noteA->startFrame - noteB->startFrame;
}

float getActiveNotes(float currentTime, int noteCount, note* noteStorage)
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
		float targetAmplitude = decimalNumberValue * decimalPercent + wholeNumberValue * (1 - decimalPercent);
		float smoothedAmplitude = prevAmp[i];
		if (smoothedAmplitude < 0.0001f && targetAmplitude <= 0.0f)
		{
			smoothedAmplitude = 0.0f;
			prevAmp[i] = 0.0f;
			phase[i] += 2 * PI * noteStorage[i].fundamental / 44100.0f;
			if (phase[i] >= 2 * PI)
			{
				phase[i] -= 2 * PI;
			}
			continue;
		}
		float rampRate = targetAmplitude > smoothedAmplitude ? 0.03f : 0.008f;
		smoothedAmplitude += (targetAmplitude - smoothedAmplitude) * rampRate;
		if (targetAmplitude <= 0.0f && smoothedAmplitude < 0.0001f)
		{
			smoothedAmplitude = 0.0f;
		}
		prevAmp[i] = smoothedAmplitude;
		mixedAmplitude += sin(phase[i]) * smoothedAmplitude;
		phase[i] += 2 * PI * noteStorage[i].fundamental / 44100.0f;
		if (phase[i] >= 2 * PI)
		{
			phase[i] -= 2 * PI;
		}
	}
	return(mixedAmplitude);
}
