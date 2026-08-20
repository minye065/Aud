#include <stdint.h>
#include <math.h>
#include "app.h"

float* phase;
//	phase = malloc(noteCount * sizeof(float))

float getActiveNotes(float currentTime, char* input)
{
	float mixedAmplitude = 0.0f;
	for (int i = 0; i < noteCount; i++)
	{
		note thisNote = noteStorage[i];
		if (thisNote.startTime > currentTime) break;
		if (thisNote.endTime < currentTime) continue;
		float timeUntilNote = currentTime - thisNote.startTime;
		float hopDuration = 1024.0f / 44100.0f;
		float position = timeUntilNote / hopDuration;
		float wholeNumberValue = noteStorage[i].envelope[floor(position)];
		float decimalNumberValue = 0;
		if ((floor(position)) + 1 < thisNote.envelopeLength)
		{
			decimalNumberValue = noteStorage[i].envelope[floor(position) + 1];
		}
		float decimalPercent = (position) - floor(position);
		mixedAmplitude += sin(phase[i]) * (decimalNumberValue * decimalPercent) + (wholeNumberValue * (1 - decimalPercent));
		phase[i] += 2 * PI * thisNote.fundamental / 44100.0f;
		if (phase[i] >= 2 * PI) 
		{
			phase[i] -= 2 * PI;
		}
	}
	return(mixedAmplitude);
}