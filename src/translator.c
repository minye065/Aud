#include <stdlib.h>
#include <string.h>
#include "app.h"

static void freePartialNotes(note* noteStorage, int parsedCount)
{
	for (int i = 0; i < parsedCount; i++)
	{
		if (noteStorage[i].envelope)
		{
			free(noteStorage[i].envelope);
		}
	}
	free(noteStorage);
}

note* translate(char* text, int* outNoteCount)
{
	int sampleRate = 44100;
	int hopSize = 1024;
	int noteCount = 0;
	*outNoteCount = 0;
	char* linePtr = strtok(text, "\r\n");
	if (!linePtr || sscanf(linePtr, "SONG %d %d %d", &sampleRate, &hopSize, &noteCount) != 3 || noteCount <= 0)
	{
		return NULL;
	}
	note* noteStorage = malloc(noteCount * sizeof(note));
	if (!noteStorage)
	{
		return NULL;
	}
	memset(noteStorage, 0, noteCount * sizeof(note));
	int previousStartFrame = 0;
	for (int i = 0; i < noteCount; i++)
	{
		linePtr = strtok(NULL, "\r\n");
		float fundamental = 0;
		int relativeStartFrame = 0;
		int durationFrames = 0;
		int envelopeLength = 0;
		if (!linePtr || sscanf(linePtr, "NOTE %f %d %d %d", &fundamental, &relativeStartFrame, &durationFrames, &envelopeLength) != 4)
		{
			freePartialNotes(noteStorage, i);
			return NULL;
		}
		note thisNote = { 0 };
		previousStartFrame += relativeStartFrame;
		thisNote.fundamental = fundamental;
		thisNote.startFrame = previousStartFrame;
		thisNote.endFrame = previousStartFrame + durationFrames;
		thisNote.envelopeLength = envelopeLength;
		thisNote.startTime = (float)thisNote.startFrame * hopSize / sampleRate;
		thisNote.endTime = (float)thisNote.endFrame * hopSize / sampleRate;
		thisNote.timeLength = thisNote.endTime - thisNote.startTime;
		if (envelopeLength > 0)
		{
			thisNote.envelope = malloc(envelopeLength * sizeof(float));
			if (!thisNote.envelope)
			{
				freePartialNotes(noteStorage, i);
				return NULL;
			}
			linePtr = strtok(NULL, "\r\n");
			if (!linePtr)
			{
				free(thisNote.envelope);
				freePartialNotes(noteStorage, i);
				return NULL;
			}
			char* p = linePtr;
			float runningValue = 0;
			for (int j = 0; j < envelopeLength; j++)
			{
				runningValue += strtof(p, &p);
				thisNote.envelope[j] = runningValue;
			}
		}
		noteStorage[i] = thisNote;
	}
	qsort(noteStorage, noteCount, sizeof(note), noteOrdering);
	float loudestMagnitude = 0;
	for (int i = 0; i < noteCount; i++)
	{
		for (int f = 0; f < noteStorage[i].envelopeLength; f++)
		{
			if (noteStorage[i].envelope[f] > loudestMagnitude)
			{
				loudestMagnitude = noteStorage[i].envelope[f];
			}
		}
	}
	if (loudestMagnitude > 0)
	{
		for (int i = 0; i < noteCount; i++)
		{
			for (int f = 0; f < noteStorage[i].envelopeLength; f++)
			{
				noteStorage[i].envelope[f] = noteStorage[i].envelope[f] / loudestMagnitude;
			}
		}
	}
	*outNoteCount = noteCount;
	return(noteStorage);
}
