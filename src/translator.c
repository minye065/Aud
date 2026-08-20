#include <stdlib.h>
#include <string.h>
#include "app.h"

note translate(char* text)
{
	note thisNote;
	int sampleRate, hopSize, frameSize, noteCount;
	char* linePtr = strtok(text, "\r\n");
	sscanf(linePtr, "HEADER %d %d %d %d", &sampleRate, &hopSize, &frameSize, &noteCount);
	note* noteStorage = malloc(noteCount * sizeof(note));
	for (int i = 0; i < noteCount; i++)
	{
		linePtr = strtok(NULL, "\r\n");
		sscanf(linePtr, "NOTE %f %d %d %d", &thisNote.fundamental, &thisNote.startFrame, &thisNote.endFrame, &thisNote.envelopeLength);
		thisNote.envelope = malloc(thisNote.envelopeLength * sizeof(float));
		thisNote.startTime = (float)thisNote.startFrame * hopSize / sampleRate;
		thisNote.endTime = (float)thisNote.endFrame * hopSize / sampleRate;
		thisNote.timeLength = thisNote.endTime - thisNote.startTime;
		linePtr = strtok(NULL, "\r\n");
		char* p = linePtr;
		for (int j = 0; j < thisNote.envelopeLength; j++)
		{
			thisNote.envelope[j] = strtof(p, &p);
		}
		noteStorage[i] = thisNote;
	}
	qsort(noteStorage, noteCount, sizeof(note), noteOrdering);
	int loudestMagnitude = 0;
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
	for (int i = 0; i < noteCount; i++)
	{
		for (int f = 0; f < noteStorage[i].envelopeLength; f++)
		{
			noteStorage[i].envelope[f] = noteStorage[i].envelope[f] / loudestMagnitude;
		}
	}
	return(noteStorage);
}