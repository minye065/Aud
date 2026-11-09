#include <stdint.h>
#include <math.h>
#include "app.h"

int noteOrdering(const void* a, const void* b)
{
	note* noteA = (note*)a;
	note* noteB = (note*)b;
	return noteA->startFrame - noteB->startFrame;
}

float getActiveNotes(song* currentSong, int16_t* returnSamples, int samplesNeeded, int64_t startSample)
{
	double time;
	for(int i = 0; i < samplesNeeded; i++)
	{
		time = (double)(startSample + i) / currentSong->sampleRate;
		float summedSampleValue = 0.0f;
		note currentNote;
		for(int j = 0; j < currentSong->noteCount; j++)
		{
			currentNote = currentSong->notes[j];
			item currentNoteItem = currentSong->items[currentNote.itemIdentifier];
			double startTime = (double)currentNote.startFrame * currentSong->hop / currentSong->sampleRate;
			double endTime = (double)currentNote.endFrame * currentSong->hop / currentSong->sampleRate;
			if(time < startTime || time >= endTime + currentNoteItem.release) continue;
			float currentNoteFrequency = currentNoteItem.startingPitch * (float)pow(2.0, currentNote.tone / 1200.0);

			double noteElapsedTime = time - startTime;
			double noteHeldDuration = endTime - startTime;
			double envelopeTime = noteElapsedTime < noteHeldDuration ? noteElapsedTime : noteHeldDuration;
			float noteVolume;
			if(envelopeTime < currentNoteItem.attack)
			{
				noteVolume = (float)(envelopeTime / currentNoteItem.attack);
			}
			else if(envelopeTime < currentNoteItem.attack + currentNoteItem.decay)
			{
				double decayElapsedTime = envelopeTime - currentNoteItem.attack;
				noteVolume = 1.0f - (1.0f - currentNoteItem.sustain) * (float)(decayElapsedTime / currentNoteItem.decay);
			}
			else
			{
				noteVolume = currentNoteItem.sustain;
			}
			if(noteElapsedTime > noteHeldDuration)
			{
				double releaseElapsedTime = noteElapsedTime - noteHeldDuration;
				noteVolume *= 1.0f - (float)(releaseElapsedTime / currentNoteItem.release);
			}

			noteVolume *= currentNote.gain;
			for(int h = 0; h < currentNoteItem.harmonicsCount; h++)
			{
				double harmonicPhase = 2 * PI * currentNoteFrequency * (h + 1) * (time - startTime);
				summedSampleValue += noteVolume * currentNoteItem.harmonics[h] * (float)sin(harmonicPhase);
			}
		}
		if(summedSampleValue > 1.0f) summedSampleValue = 1.0f;
		if(summedSampleValue < -1.0f) summedSampleValue = -1.0f;
		int16_t scaledTotal = (int16_t)(summedSampleValue * 32767);
		returnSamples[i] = scaledTotal;
	}
	return 0.0f;
}
