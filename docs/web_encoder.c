//encoder.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "hannTable.c"
#include "kissfft/kiss_fftr.h"

typedef struct peakStorage
{
	int length; //of run
	float peakHz;
	int startFrame;
	int endFrame;
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
	printf("totalSize=%u sampleRate=%u amountOfFrames=%u\n", totalSize, sampleRate, amountOfFrames);
	float* frameStorage = malloc(amountOfFrames * frameSize * sizeof(float));
	float* windowedFrameStorage = malloc(amountOfFrames * frameSize * sizeof(float));

	if (frameStorage && windowedFrameStorage)
	{
		for (int k = 0; k < amountOfFrames; k++)
		{
			for (int i = 0; i < frameSize; i++)
			{
				unsigned int PCMPosition = k * hopSize + i;
				frameStorage[k * frameSize + i] = passedInPCM[PCMPosition];
			}
		}

		for (int k = 0; k < amountOfFrames; k++)
		{
			for (int i = 0; i < frameSize; i++)
			{
				windowedFrameStorage[k * frameSize + i] = frameStorage[k * frameSize + i] * hannTable[i];
			}
		}
		kiss_fft_cpx spectrum[binSize];
		float* magnitudeStorage = malloc(amountOfFrames * binSize * sizeof(float)); 
		float* peakHzStorage = malloc (amountOfFrames * sizeof(float));
		for (int k = 0; k < amountOfFrames; k++)
		{
			kiss_fftr(cfg, &windowedFrameStorage[k * frameSize], spectrum);

			int bin = 0;
			float highestMagnitude = 0;

			for (int b = 0; b < binSize; b++)
			{
				magnitudeStorage[k * binSize + b] = sqrtf(spectrum[b].r * spectrum[b].r + spectrum[b].i * spectrum[b].i);
				float binMagnitude = magnitudeStorage[k * binSize + b];
				if (binMagnitude > highestMagnitude)
				{
					highestMagnitude = binMagnitude;
					bin = b;
				}
			}

			float peakHz = bin * sampleRate / frameSize;
			peakHzStorage[k] = peakHz;
			float peakPhaseStorage = atan2f(spectrum[bin].i, spectrum[bin].r);
			float peakReal = highestMagnitude * cosf(peakPhaseStorage);
			float peakImaginary = highestMagnitude * sinf(peakPhaseStorage);
			spectrum[bin].i -= peakImaginary;
			spectrum[bin].r -= peakReal;
		}

		float upperbound = peakHzStorage[0] + 20;
		float lowerbound = peakHzStorage[0] - 20;
		int frameCounter = 1;
		int partialTrackCounter = 0;
		peakStorage thisIterationPeak;
		peakStorage* partialTrackStorage = malloc(amountOfFrames * sizeof(peakStorage));
		int groupCounter = 0;
		int startFrame = 0;
		for (int k = 1; k < amountOfFrames; k++)
		{
			if (peakHzStorage[k] < upperbound && peakHzStorage[k] > lowerbound)
			{
				frameCounter += 1;
			}
			else
			{
				upperbound = peakHzStorage[k] + 20;
				lowerbound = peakHzStorage[k] - 20;
				thisIterationPeak.length = frameCounter;
				thisIterationPeak.peakHz = peakHzStorage[k - 1];
				thisIterationPeak.startFrame = startFrame;
				thisIterationPeak.endFrame = k - 1;
				partialTrackStorage[partialTrackCounter] = thisIterationPeak;
				startFrame = k;
				partialTrackCounter += 1;
				frameCounter = 1;
			}
		}
		printf("partialTracks=%d\n", partialTrackCounter);
		thisIterationPeak.length = frameCounter;
		thisIterationPeak.peakHz = peakHzStorage[amountOfFrames - 1];
		thisIterationPeak.startFrame = startFrame;
		thisIterationPeak.endFrame = amountOfFrames - 1;
		free(peakHzStorage);
		partialTrackStorage[partialTrackCounter] = thisIterationPeak;
		partialTrackCounter += 1;

		int (*groupedFrequencyRuns)[2] = malloc(partialTrackCounter * sizeof(int[2]));

		int* partialTracksLeft = malloc(partialTrackCounter * sizeof(int));
		int numberOfTracksLeft = partialTrackCounter;
		for (int t = 0; t < partialTrackCounter; t++)
		{
			partialTracksLeft[t] = t;
		}
		
		for (int p = 0; p < numberOfTracksLeft; p++)
		{
			int targetTrack = partialTracksLeft[p];

			float startingTargetHz = partialTrackStorage[targetTrack].peakHz;
			int targetStartFrame = partialTrackStorage[targetTrack].startFrame;
			int targetEndFrame = partialTrackStorage[targetTrack].endFrame;

			int score = 0;
			int topScore = 0;
			int topIteration = 0;
			int matchBOOL = 0;

			for (int i = 0; i < numberOfTracksLeft; i++)
			{
				int thisTrack = partialTracksLeft[i];
				if (thisTrack == targetTrack) continue;

				int thisStartFrame = partialTrackStorage[thisTrack].startFrame;
				int thisEndFrame = partialTrackStorage[thisTrack].endFrame;
				//harmonics
				int harmonicsCheck = 0;
				for (int m = 1; m < 11; m++)
				{
					if (partialTrackStorage[thisTrack].peakHz > (startingTargetHz * m) - 10 && partialTrackStorage[thisTrack].peakHz < (startingTargetHz * m) + 10)
					{
						harmonicsCheck = 1;
					}
				}
				if (harmonicsCheck == 1)
				{
					score += 1;
				}
				//sim start/end
				if (targetStartFrame == thisStartFrame || targetEndFrame == thisEndFrame)
				{
					score += 1;
				}

				if (score > topScore) // replace with clustering
				{
					topScore = score;
					topIteration = i;
					matchBOOL = 1;
				}
				else if (score == topScore)
				{
					//skill issue ig?
					//need more sophisticated system
				}
				if ((i == (numberOfTracksLeft - 1)) && (matchBOOL == 0))
				{
					topIteration = -4;
				}
				score = 0;
			}

			//end storage
			if(topIteration == -4)
			{
				groupedFrequencyRuns[groupCounter][0] = targetTrack;
				groupedFrequencyRuns[groupCounter][1] = -4;
				groupCounter += 1;
			}
			else
			{
				groupedFrequencyRuns[groupCounter][0] = targetTrack;
				groupedFrequencyRuns[groupCounter][1] = partialTracksLeft[topIteration]; // bit shifting instead?
				if (topIteration > p)
				{
					for (int i = topIteration; i < numberOfTracksLeft - 1; i++)
					{
						partialTracksLeft[i] = partialTracksLeft[i + 1];
					}
					numberOfTracksLeft -= 1;
					for (int i = p; i < numberOfTracksLeft -1; i++)
					{
						partialTracksLeft[i] = partialTracksLeft[i + 1];
					}
					numberOfTracksLeft -= 1;
				}
				else
				{
					for (int i = p; i < numberOfTracksLeft - 1; i++)
					{
						partialTracksLeft[i] = partialTracksLeft[i + 1];
					}
					numberOfTracksLeft -= 1;
					for (int i = topIteration; i < numberOfTracksLeft - 1; i++)
					{
						partialTracksLeft[i] = partialTracksLeft[i + 1];
					}
					numberOfTracksLeft -= 1;
				}
				p -= 1;
				groupCounter += 1;
			}
			if (p % 500 == 0) printf("grouping p=%d of %d\n", p, numberOfTracksLeft);
		}
		free(partialTracksLeft);
		int groupStartFrame;
		int groupEndFrame;
		int maxGroupLength = 0;
		for (int i = 0; i < groupCounter; i++)
		{
			if (groupedFrequencyRuns[i][1] != -4)
			{
				int targetRun1 = groupedFrequencyRuns[i][0];
				int targetRun2 = groupedFrequencyRuns[i][1];
				if (partialTrackStorage[targetRun1].startFrame < partialTrackStorage[targetRun2].startFrame)
				{
					groupStartFrame = partialTrackStorage[targetRun1].startFrame;
				}
				else
				{
					groupStartFrame = partialTrackStorage[targetRun2].startFrame;
				}
				if (partialTrackStorage[targetRun1].endFrame < partialTrackStorage[targetRun2].endFrame)
				{
					groupEndFrame = partialTrackStorage[targetRun1].endFrame;
				}
				else
				{
					groupEndFrame = partialTrackStorage[targetRun2].endFrame;
				}

				if ((groupEndFrame - groupStartFrame + 1) > maxGroupLength)
				{
					maxGroupLength = (groupEndFrame - groupStartFrame + 1);
				}
			}
			else
			{
				int targetRun = groupedFrequencyRuns[i][0];
				groupStartFrame = partialTrackStorage[targetRun].startFrame;
				groupEndFrame = partialTrackStorage[targetRun].endFrame;
				if ((groupEndFrame - groupStartFrame + 1) > maxGroupLength)
				{
					maxGroupLength = (groupEndFrame - groupStartFrame + 1);
				}
			}
		}

		float* groupEnvelope = malloc(groupCounter * maxGroupLength * sizeof(float));
		noteEvent* noteEventStorage = malloc(groupCounter * sizeof(noteEvent));
		for (int i = 0; i < groupCounter; i++)
		{
			noteEvent thisNoteEvent;
			if(groupedFrequencyRuns[i][1] != -4)
			{
				int targetRun1 = groupedFrequencyRuns[i][0];
				int targetRun2 = groupedFrequencyRuns[i][1];
				int frequency1 = partialTrackStorage[targetRun1].peakHz;
				int frequency2 = partialTrackStorage[targetRun2].peakHz;

				if (frequency1 < frequency2)
				{
					thisNoteEvent.fundamental = frequency1;
				}
				else
				{
					thisNoteEvent.fundamental = frequency2;
				}

				if (partialTrackStorage[targetRun1].startFrame < partialTrackStorage[targetRun2].startFrame)
				{
					groupStartFrame = partialTrackStorage[targetRun1].startFrame;
				}
				else
				{
					groupStartFrame = partialTrackStorage[targetRun2].startFrame;
				}
				if (partialTrackStorage[targetRun1].endFrame < partialTrackStorage[targetRun2].endFrame)
				{
					groupEndFrame = partialTrackStorage[targetRun1].endFrame;
				}
				else
				{
					groupEndFrame = partialTrackStorage[targetRun2].endFrame;
				}
				thisNoteEvent.startFrame = groupStartFrame;
				thisNoteEvent.endFrame = groupEndFrame;
				for (int s = groupStartFrame; s <= groupEndFrame; s++)
				{
					float frameMagnitude = 0;
					if (s >= partialTrackStorage[targetRun1].startFrame && s <= partialTrackStorage[targetRun1].endFrame)
					{
						int bin1 = round(partialTrackStorage[targetRun1].peakHz * frameSize / sampleRate);
						frameMagnitude += magnitudeStorage[s * binSize + bin1];
					}
					if (s >= partialTrackStorage[targetRun2].startFrame && s <= partialTrackStorage[targetRun2].endFrame)
					{
						int bin2 = round(partialTrackStorage[targetRun2].peakHz * frameSize / sampleRate);
						frameMagnitude += magnitudeStorage[s * binSize + bin2];
					}
					groupEnvelope[i * maxGroupLength + (s - groupStartFrame)] = frameMagnitude;
				}
				thisNoteEvent.envelope = &groupEnvelope[i * maxGroupLength];
				thisNoteEvent.envelopeLength = groupEndFrame - groupStartFrame + 1;
			}
			else
			{
				int targetRun1 = groupedFrequencyRuns[i][0];
				int frequency1 = partialTrackStorage[targetRun1].peakHz;
				thisNoteEvent.fundamental = frequency1;

				int targetRun = groupedFrequencyRuns[i][0];
				groupStartFrame = partialTrackStorage[targetRun].startFrame;
				groupEndFrame = partialTrackStorage[targetRun].endFrame;

				for (int s = groupStartFrame; s <= groupEndFrame; s++)
				{
					float frameMagnitude = 0;
					if (s >= partialTrackStorage[targetRun].startFrame && s <= partialTrackStorage[targetRun].endFrame)
					{
						int bin1 = round(partialTrackStorage[targetRun].peakHz * frameSize / sampleRate);
						frameMagnitude += magnitudeStorage[s * binSize + bin1];
					}
					groupEnvelope[i * maxGroupLength + (s - groupStartFrame)] = frameMagnitude;
				}
				thisNoteEvent.envelope = &groupEnvelope[i * maxGroupLength];
				thisNoteEvent.envelopeLength = groupEndFrame - groupStartFrame + 1;
			}
			thisNoteEvent.itemID = i;
			noteEventStorage[i] = thisNoteEvent;
		}
		noteCount = groupCounter;
		envelopeBuffer = groupEnvelope;
		result = noteEventStorage;
		free(magnitudeStorage);
		free(partialTrackStorage);
		free(groupedFrequencyRuns);
	}
	kiss_fftr_free(cfg);
	free(frameStorage);
	free(windowedFrameStorage);
	return(result);
}

int get_note_count() { return noteCount; }
float* get_envelope_ptr() { return envelopeBuffer; }