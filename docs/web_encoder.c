//encoder.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "hannTable.c"
#include "kissfft_min/kiss_fftr.h"

#define pi 3.14159265359

typedef struct sector
{
	float pitch;
	float magnitude;
	int duration;
}sector;

typedef struct song
{
	sector* writebuffer;
	unsigned int bufferelementcount;
}song;

song encode(int16_t* passedInPCM) // ignore for now, handle type change later
{
	song final;
	unsigned int totalSize; //FROM PCM
	unsigned int sampleRate; //FROM PCM
	unsigned int frameSize = 2048;
	unsigned int hopSize = 1024;
	unsigned int binSize = 1025;
	unsigned int amountOfFrames;
	kiss_fftr_cfg cfg = kiss_fftr_alloc(2048, 0, NULL, NULL);

	if (totalSize > frameSize)
	{
		amountOfFrames = ((totalSize - frameSize) / hopSize) + 1;
	}
	else
	{
		amountOfFrames = 1;
	}

	int16_t* frameStorage = malloc(amountOfFrames * frameSize * sizeof(int16_t));
	float* windowedFrameStorage = malloc(amountOfFrames * frameSize * sizeof(float));

	if (frameStorage && windowedFrameStorage)
	{
		for (int k = 0; k < amountOfFrames; k++)
		{
			for (int i = 0; i < frameSize; i++)
			{
				unsigned int PCMPosition = k * hopSize + i;
				frameStorage[k * frameSize + i] = passedInPCM[PCMPosition]; //turn into malloc
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

		for (int k = 0; k < amountOfFrames; k++)
		{
			kiss_fftr(cfg, &windowedFrameStorage[k * frameSize], spectrum);

			for (int b = 0; b < binSize; b++)
			{
				magnitudeStorage[k * binSize + b] = sqrtf(spectrum[b].r * spectrum[b].r + spectrum[b].i * spectrum[b].i);
			}
		}

		sector* sectorStorage = malloc(amountOfFrames * sizeof(sector));
		sector finalSector;
		int pitchHz = 0;
		float magnitude = 0;
		int targetPitch = 0;
		int pitchRunningCount = 0;
		int sectorRunningCount = 0;
		float totalFrameMagnitude = 0;
		float totalMagnitude = 0;
		for (int k = 0; k < amountOfFrames; k++)
		{
			//magnitude
			totalFrameMagnitude = 0;
			for (int i = 0; i < binSize; i++)
			{
				totalFrameMagnitude += magnitudeStorage[k * binSize + i];
			}
			sector sectorOfThisIteration;
			//pitch
			float highestMagnitude = 0;
			int binIndex = 0;
			for (int i = 0; i < binSize; i++)
			{
				float thisMagnitude = magnitudeStorage[k * binSize + i];
				if (thisMagnitude > highestMagnitude)
				{
					highestMagnitude = thisMagnitude;
					binIndex = i;
				}
			}
			pitchHz = binIndex * sampleRate / frameSize;
			//duration
			if (k == 0)
			{
				targetPitch = pitchHz;
				pitchRunningCount = 1;
				//magnitude
				totalMagnitude += totalFrameMagnitude;
			}
			else if (targetPitch == pitchHz)
			{
				pitchRunningCount += 1;
				totalMagnitude += totalFrameMagnitude;

			}
			else
			{
				magnitude = (totalMagnitude) / (binSize * pitchRunningCount);
				sectorOfThisIteration.magnitude = magnitude;
				totalMagnitude = totalFrameMagnitude;

				sectorOfThisIteration.duration = pitchRunningCount;
				sectorOfThisIteration.pitch = targetPitch;
				sectorStorage[sectorRunningCount] = sectorOfThisIteration;
				sectorRunningCount++;
				//reset pitch loop
				targetPitch = pitchHz;
				pitchRunningCount = 1;
			}
		}
		magnitude = totalMagnitude / (binSize * pitchRunningCount);
		finalSector.pitch = targetPitch;
		finalSector.magnitude = magnitude;
		finalSector.duration = pitchRunningCount;
		sectorStorage[sectorRunningCount] = finalSector;
		sectorRunningCount += 1;
		sectorStorage = realloc(sectorStorage, sectorRunningCount * sizeof(sector));
	}


	kiss_fftr_free(cfg);
	return final;
}
//free framestorage windowedframestorage magnitudestorage
int main()
{
}