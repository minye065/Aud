//encoder.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "hannTable.c"
#include "kissfft/kiss_fftr.h"

typedef struct item
{
	char name[32];
	float harmonics[16];
	bool isTonal;
	float startingPitch;
	int harmonicsCount;
	float lowestFrequency;
	float highestFrequency;
	float attack;
	float sustain;
	float release;
	float decay;
}item;

typedef struct note
{
	int itemIdentifier;
	float tone;
	int startFrame;
	int endFrame;
	float gain;
}note;

typedef struct song
{
	char name[32];
	int sampleRate;
	int hop;
	int itemCount;
	item items[32];
	int noteCount;
	int noteCapacity;
	note* notes;
}song;


typedef struct peakStorage
{
	int length;
	float peakHz;
	int startFrame;
	int endFrame;
	float lastHz;
	float totalHz;
}peakStorage;

typedef struct noteEvent
{
	float fundamental;
	int startFrame;
	int endFrame;
	float* envelope;
	int envelopeLength;
	int itemID;
}noteEvent;

static int noteCount = 0;
static float* envelopeBuffer = NULL;

static int hzToBin(float hz, unsigned int frameSize, unsigned int sampleRate, unsigned int binSize)
{
	int bin = (int)roundf(hz * frameSize / sampleRate);
	if (bin < 0) bin = 0;
	if (bin > (int)binSize - 1) bin = (int)binSize - 1;
	return bin;
}

static int pickPeaks(float* magnitudeStorage, int binCount, unsigned int sampleRate, unsigned int frameSize, float *peakHzAndMagnitudeOutput)
{
	float targetMagnitude = 0;
	float highestMagnitude = 0;
	int binWithHighestMagnitude = 0;
	int peakCount = 0;
	int peakBins[8];
	float peakMagnitude[8];
	for(int i = 0; i < binCount; i++)
	{
		targetMagnitude = magnitudeStorage[i];

		if(targetMagnitude > highestMagnitude)
		{
			highestMagnitude = targetMagnitude;
			binWithHighestMagnitude = i;
		}
	}
	float magnitudeFloor = highestMagnitude * 0.01;
	for(int i = 1; i < binCount - 1; i++)
	{
		if(magnitudeStorage[i] <= magnitudeFloor) continue;
		if(magnitudeStorage[i] > magnitudeStorage[i + 1] && magnitudeStorage[i] > magnitudeStorage[i - 1])
		{
			if(peakCount < 8)
			{
				peakBins[peakCount] = i;
				peakMagnitude[peakCount] = magnitudeStorage[i];
				peakCount++;
			}
			else
			{
					int indexOfSmallestPeak = 0;
					for(int k = 1; k < 8; k++)
					{
							if(peakMagnitude[k] < peakMagnitude[indexOfSmallestPeak])
							{
									indexOfSmallestPeak = k;
							}
					}
					if(magnitudeStorage[i] > peakMagnitude[indexOfSmallestPeak])
					{
							peakBins[indexOfSmallestPeak] = i;
							peakMagnitude[indexOfSmallestPeak] = magnitudeStorage[i];
					}
			}
		}
	}

	for(int i = 0; i < peakCount; i++)
	{
		int bin = peakBins[i];

		float leftMag = bin > 0 ? magnitudeStorage[bin - 1] : 0.0f;
		float rightMag = bin < (int)binCount - 1 ? magnitudeStorage[bin+ 1] : 0.0f;
		float centerMag = magnitudeStorage[bin];
		float parabolaDenominator = leftMag - 2 * centerMag + rightMag;
		float binShift = parabolaDenominator != 0.0f ? 0.5f * (leftMag - rightMag) / parabolaDenominator : 0.0f;
		if (binShift > 1.0f) binShift = 1.0f;
		if (binShift < -1.0f) binShift = -1.0f;
		peakHzAndMagnitudeOutput[i * 2] = ((float)bin + binShift) * sampleRate / frameSize;
		peakHzAndMagnitudeOutput[i * 2 + 1] = peakMagnitude[i];
	}
	return(peakCount);
}

int linkTracks(float* peakHzAndMagnitudeStorage, int* peakCounts, int amountOfFrames, peakStorage* trackStorage)
{
	int activeTrackIndexes[64];
	int activeTrackCount = 0;
	int trackCount = 0;
	for (int k = 0; k < amountOfFrames; k++)
	{
		int keptTracksCount = 0;
		for (int i = 0; i < activeTrackCount; i++)
		{
			if (k - trackStorage[activeTrackIndexes[i]].endFrame <= 3)
			{
				activeTrackIndexes[keptTracksCount] = activeTrackIndexes[i];
				keptTracksCount++;
			}
		}
		activeTrackCount = keptTracksCount;

		for (int p = 0; p < peakCounts[k]; p++)
		{
			float peakHz = peakHzAndMagnitudeStorage[k * 16 + p * 2];
			int bestTrackIndex = -1;
			float bestCentsDistance = 50.0f;
			for (int i = 0; i < activeTrackCount; i++)
			{
				int candidateTrackIndex = activeTrackIndexes[i];
				if (trackStorage[candidateTrackIndex].endFrame == k) continue;
				float centsDistance = fabsf(1200 * log2f(peakHz / trackStorage[candidateTrackIndex].lastHz));
				if (centsDistance < bestCentsDistance)
				{
					bestCentsDistance = centsDistance;
					bestTrackIndex = candidateTrackIndex;
				}
			}
			if (bestTrackIndex >= 0)
			{
				trackStorage[bestTrackIndex].endFrame = k;
				trackStorage[bestTrackIndex].lastHz = peakHz;
				trackStorage[bestTrackIndex].totalHz += peakHz;
				trackStorage[bestTrackIndex].length += 1;
			}
			else
			{
				trackStorage[trackCount].startFrame = k;
				trackStorage[trackCount].endFrame = k;
				trackStorage[trackCount].length = 1;
				trackStorage[trackCount].lastHz = peakHz;
				trackStorage[trackCount].totalHz = peakHz;
				activeTrackIndexes[activeTrackCount] = trackCount;
				activeTrackCount++;
				trackCount++;
			}
		}
	}
	for (int t = 0; t < trackCount; t++)
	{
		trackStorage[t].peakHz = trackStorage[t].totalHz / trackStorage[t].length;
	}
	return trackCount;
}

noteEvent* encode(float* passedInPCM, unsigned int totalSize, unsigned int sampleRate)
{
	unsigned int frameSize = 2048;
	unsigned int hopSize = 1024;
	unsigned int binSize = 1025;
	unsigned int amountOfFrames;
	noteEvent* result = NULL;
	kiss_fftr_cfg cfg = kiss_fftr_alloc(2048, 0, NULL, NULL);

	if (totalSize > frameSize)
	{
		amountOfFrames = ((totalSize - frameSize) / hopSize) + 1;
	}
	else
	{
		amountOfFrames = 1;
	}
	float* frameBuffer = malloc(frameSize * sizeof(float));

	if(frameBuffer && cfg)
	{
		kiss_fft_cpx spectrum[binSize];
		float* magnitudeStorage = malloc(amountOfFrames * binSize * sizeof(float));
		float* peakHzAndMagnitudeStorage = malloc(amountOfFrames * 16 * sizeof(float));
		int* peakCounts = malloc(amountOfFrames * sizeof(int));
		

		if (magnitudeStorage && peakHzAndMagnitudeStorage && peakCounts)
		{
			for (int k = 0; k < amountOfFrames; k++)
			{
				for (int i = 0; i < frameSize; i++)
				{
					unsigned int PCMPosition = k * hopSize + i;
					frameBuffer[i] = PCMPosition < totalSize ? passedInPCM[PCMPosition] * hannTable[i] : 0.0f;
				}
				kiss_fftr(cfg, frameBuffer, spectrum);
				for(int i = 0; i < binSize; i++)
				{
					magnitudeStorage[k * binSize + i] = sqrtf(spectrum[i].r * spectrum[i].r + spectrum[i].i * spectrum[i].i);
				}
				peakCounts[k] = pickPeaks(magnitudeStorage + k * binSize, binSize, sampleRate, frameSize, peakHzAndMagnitudeStorage + k * 16);
			}
			int totalPeakCount = 0;
			for (int k = 0; k < amountOfFrames; k++)
			{
					totalPeakCount += peakCounts[k];
			}
			peakStorage* trackStorage = totalPeakCount > 0 ? malloc(totalPeakCount * sizeof(peakStorage)): NULL;
			if (trackStorage)
			{
				int trackCount = linkTracks(peakHzAndMagnitudeStorage, peakCounts, amountOfFrames, trackStorage);
				int groupStartFrame;
				int groupEndFrame;
				int totalEnvelopeLength = 0;
				int groupCounter = 0;
				int* groupOfTrack = malloc(trackCount * sizeof(int));
				int* groupFirstTrack = malloc(trackCount * sizeof(int));
				int* groupEnvelopeOffsets = malloc(trackCount * sizeof(int));
				int* groupStartFrames = malloc(trackCount * sizeof(int));
				int* groupEndFrames = malloc(trackCount * sizeof(int));

				if (groupFirstTrack && groupOfTrack && groupEnvelopeOffsets && groupStartFrames && groupEndFrames)
				{
					for (int t = 0; t < trackCount; t++)
					{
							groupOfTrack[t] = -1;
					}
					for (int t = 0; t < trackCount; t++)
					{
						if (groupOfTrack[t] != -1) continue;
						groupOfTrack[t] = groupCounter;
						groupFirstTrack[groupCounter] = t;
						for (int u = t + 1; u < trackCount; u++)
						{
							if (trackStorage[u].startFrame > trackStorage[t].startFrame + 1) break;
							if (groupOfTrack[u] != -1) continue;
							float harmonicRatio = trackStorage[u].peakHz / trackStorage[t].peakHz;
							for (int m = 2; m < 9; m++)
							{
								if (fabsf(1200 * log2f(harmonicRatio / m)) < 50)
								{
									groupOfTrack[u] = groupCounter;
									break;
								}
							}
						}
						groupCounter++;
					}
					for (int g = 0; g < groupCounter; g++)
					{
						groupStartFrames[g] = amountOfFrames;
						groupEndFrames[g] = 0;
				}
					for (int t = 0; t < trackCount; t++)
					{
						int g = groupOfTrack[t];
						if (trackStorage[t].startFrame < groupStartFrames[g])
						{
								groupStartFrames[g] = trackStorage[t].startFrame;
						}
						if (trackStorage[t].endFrame > groupEndFrames[g])
						{
								groupEndFrames[g] = trackStorage[t].endFrame;
						}
					}
					for (int g = 0; g < groupCounter; g++)
					{
						groupEnvelopeOffsets[g] = totalEnvelopeLength;
						totalEnvelopeLength += groupEndFrames[g] - groupStartFrames[g] + 1;
					}
					float* groupEnvelope = calloc(totalEnvelopeLength, sizeof(float));
					noteEvent* noteEventStorage = malloc(groupCounter * sizeof(noteEvent));

					if (groupEnvelope && noteEventStorage)
					{
						for(int t = 0; t < trackCount; t++)
						{
							int g = groupOfTrack[t];
							int bin = hzToBin(trackStorage[t].peakHz, frameSize, sampleRate, binSize);
							for (int s = trackStorage[t].startFrame; s <= trackStorage[t].endFrame; s++)
							{
								groupEnvelope[groupEnvelopeOffsets[g] + (s - groupStartFrames[g])] += magnitudeStorage[s * binSize + bin];
							}
						}
						for (int i = 0; i < groupCounter; i++)
						{
							noteEvent thisNoteEvent;
							thisNoteEvent.fundamental = trackStorage[groupFirstTrack[i]].peakHz;
							groupStartFrame = groupStartFrames[i];
							groupEndFrame = groupEndFrames[i];
							thisNoteEvent.startFrame = groupStartFrame;
							thisNoteEvent.endFrame = groupEndFrame;
							thisNoteEvent.envelopeLength = groupEndFrame - groupStartFrame + 1;
							thisNoteEvent.envelope = &groupEnvelope[groupEnvelopeOffsets[i]];
							thisNoteEvent.itemID = i;
							noteEventStorage[i] = thisNoteEvent;
						}
						noteCount = groupCounter;
						if (envelopeBuffer)
						{
							free(envelopeBuffer);
						}
						envelopeBuffer = groupEnvelope;
						result = noteEventStorage;
					}
					else
					{
						free(groupEnvelope);
						free(noteEventStorage);
					}
				}
				free(groupStartFrames);
				free(groupEndFrames);
				free(groupOfTrack);
				free(groupFirstTrack);
				free(groupEnvelopeOffsets);
			}
			free(trackStorage);
		}
		free(magnitudeStorage);
		free(peakHzAndMagnitudeStorage);
		free(peakCounts);
	}
	kiss_fftr_free(cfg);
	free(frameBuffer);
	return(result);
}

int get_note_count() { return noteCount; }
float* get_envelope_ptr() { return envelopeBuffer; }
void cleanup_envelopes()
{
	if (envelopeBuffer)
	{
		free(envelopeBuffer);
		envelopeBuffer = NULL;
	}
	noteCount = 0;
}
