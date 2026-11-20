#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "app.h"

int noteOrdering(const void* a, const void* b)
{
	note* noteA = (note*)a;
	note* noteB = (note*)b;
	return noteA->startFrame - noteB->startFrame;
}

typedef struct
{
	const note* sourceNote;
	const item* sourceItem;
	double startTime;
	double endTime;
	float frequency;
} activeNote;

static activeNote* activeNotes = NULL;
static int activeNoteCapacity = 0;

float getActiveNotes(song* currentSong, int16_t* returnSamples, int samplesNeeded, int64_t startSample)
{
	double chunkStartTime = (double)startSample / currentSong->sampleRate;
	double chunkEndTime = (double)(startSample + samplesNeeded) / currentSong->sampleRate;
	if(activeNoteCapacity < currentSong->noteCount)
	{
		activeNote* reallocatedActiveNotes = realloc(activeNotes, currentSong->noteCount * sizeof(activeNote));
		if(!reallocatedActiveNotes)
		{
			memset(returnSamples, 0, samplesNeeded * sizeof(int16_t));
			return 0.0f;
		}
		activeNotes = reallocatedActiveNotes;
		activeNoteCapacity = currentSong->noteCount;
	}
	int activeNoteCount = 0;
	for(int j = 0; j < currentSong->noteCount; j++)
	{
		const note* candidateNote = &currentSong->notes[j];
		double startTime = (double)candidateNote->startFrame * currentSong->hop / currentSong->sampleRate;
		if(startTime > chunkEndTime) break;
		const item* candidateItem = &currentSong->items[candidateNote->itemIdentifier];
		double endTime = (double)candidateNote->endFrame * currentSong->hop / currentSong->sampleRate;
		if(endTime + candidateItem->release <= chunkStartTime) continue;
		activeNote* chunkNote = &activeNotes[activeNoteCount++];
		chunkNote->sourceNote = candidateNote;
		chunkNote->sourceItem = candidateItem;
		chunkNote->startTime = startTime;
		chunkNote->endTime = endTime;
		chunkNote->frequency = candidateItem->startingPitch * (float)pow(2.0, candidateNote->tone / 1200.0);
	}

	double time;
	for(int i = 0; i < samplesNeeded; i++)
	{
		time = (double)(startSample + i) / currentSong->sampleRate;
		float summedSampleValue = 0.0f;
		for(int j = 0; j < activeNoteCount; j++)
		{
			const note* currentNote = activeNotes[j].sourceNote;
			const item* currentNoteItem = activeNotes[j].sourceItem;
			double startTime = activeNotes[j].startTime;
			double endTime = activeNotes[j].endTime;
			if(time < startTime || time >= endTime + currentNoteItem->release) continue;
			float currentNoteFrequency = activeNotes[j].frequency;

			double noteElapsedTime = time - startTime;
			double noteHeldDuration = endTime - startTime;
			double envelopeTime = noteElapsedTime < noteHeldDuration ? noteElapsedTime : noteHeldDuration;
			float noteVolume;
			if(envelopeTime < currentNoteItem->attack)
			{
				noteVolume = (float)(envelopeTime / currentNoteItem->attack);
			}
			else if(envelopeTime < currentNoteItem->attack + currentNoteItem->decay)
			{
				double decayElapsedTime = envelopeTime - currentNoteItem->attack;
				noteVolume = 1.0f - (1.0f - currentNoteItem->sustain) * (float)(decayElapsedTime / currentNoteItem->decay);
			}
			else
			{
				noteVolume = currentNoteItem->sustain;
			}
			if(noteElapsedTime > noteHeldDuration)
			{
				double releaseElapsedTime = noteElapsedTime - noteHeldDuration;
				noteVolume *= 1.0f - (float)(releaseElapsedTime / currentNoteItem->release);
			}

			noteVolume *= currentNote->gain;
			for(int h = 0; h < currentNoteItem->harmonicsCount; h++)
			{
				double harmonicPhase = 2 * PI * currentNoteFrequency * (h + 1) * (time - startTime);
				summedSampleValue += noteVolume * currentNoteItem->harmonics[h] * (float)sin(harmonicPhase);
			}
		}
		if(!(summedSampleValue == summedSampleValue)) summedSampleValue = 0.0f;
		summedSampleValue = tanhf(summedSampleValue);
		int16_t scaledTotal = (int16_t)(summedSampleValue * 32767);
		returnSamples[i] = scaledTotal;
	}
	return 0.0f;
}
