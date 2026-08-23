//encoder.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "hannTable.c"
#include "kissfft/kiss_fftr.h"

typedef struct peakStorage
{
	int length;
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

static int hzToBin(float hz, unsigned int frameSize, unsigned int sampleRate, unsigned int binSize)
{
	int bin = (int)roundf(hz * frameSize / sampleRate);
	if (bin < 0) bin = 0;
	if (bin > (int)binSize - 1) bin = (int)binSize - 1;
	return bin;
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
#ifdef HDEBUG
	printf("totalSize=%u sampleRate=%u amountOfFrames=%u\n", totalSize, sampleRate, amountOfFrames);
#endif
	float* frameBuffer = malloc(frameSize * sizeof(float));

	if (frameBuffer)
	{
		kiss_fft_cpx spectrum[binSize];
		float* magnitudeStorage = malloc(amountOfFrames * binSize * sizeof(float));
		float* peakHzStorage = malloc(amountOfFrames * sizeof(float));

		if (magnitudeStorage && peakHzStorage)
		{
			for (int k = 0; k < amountOfFrames; k++)
			{
				for (int i = 0; i < frameSize; i++)
				{
					unsigned int PCMPosition = k * hopSize + i;
					frameBuffer[i] = PCMPosition < totalSize ? passedInPCM[PCMPosition] * hannTable[i] : 0.0f;
				}
				kiss_fftr(cfg, frameBuffer, spectrum);

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

				float leftMag = bin > 0 ? magnitudeStorage[k * binSize + bin - 1] : 0.0f;
				float rightMag = bin < (int)binSize - 1 ? magnitudeStorage[k * binSize + bin + 1] : 0.0f;
				float centerMag = magnitudeStorage[k * binSize + bin];
				float parabolaDenominator = leftMag - 2 * centerMag + rightMag;
				float binShift = parabolaDenominator != 0.0f ? 0.5f * (leftMag - rightMag) / parabolaDenominator : 0.0f;
				if (binShift > 1.0f) binShift = 1.0f;
				if (binShift < -1.0f) binShift = -1.0f;
				peakHzStorage[k] = ((float)bin + binShift) * sampleRate / frameSize;
			}

			for (int k = 1; k < amountOfFrames - 1; k++)
			{
				float a = peakHzStorage[k - 1];
				float b = peakHzStorage[k];
				float c = peakHzStorage[k + 1];
				float median = a;
				if ((b >= a && b <= c) || (b <= a && b >= c)) median = b;
				else if ((c >= a && c <= b) || (c <= a && c >= b)) median = c;
				peakHzStorage[k] = median;
			}

			float tolerance = (float)sampleRate / frameSize * 1.5f;
			float upperbound = peakHzStorage[0] + tolerance;
			float lowerbound = peakHzStorage[0] - tolerance;
			int frameCounter = 1;
			float runHzTotal = peakHzStorage[0];
			int partialTrackCounter = 0;
			peakStorage thisIterationPeak;
			peakStorage* partialTrackStorage = malloc(amountOfFrames * sizeof(peakStorage));
			int groupCounter = 0;
			int startFrame = 0;

			if (partialTrackStorage)
			{
				for (int k = 1; k < amountOfFrames; k++)
				{
					if (peakHzStorage[k] < upperbound && peakHzStorage[k] > lowerbound)
					{
						frameCounter += 1;
						runHzTotal += peakHzStorage[k];
					}
					else
					{
						upperbound = peakHzStorage[k] + tolerance;
						lowerbound = peakHzStorage[k] - tolerance;
						thisIterationPeak.length = frameCounter;
						thisIterationPeak.peakHz = runHzTotal / frameCounter;
						thisIterationPeak.startFrame = startFrame;
						thisIterationPeak.endFrame = k - 1;
						partialTrackStorage[partialTrackCounter] = thisIterationPeak;
						startFrame = k;
						partialTrackCounter += 1;
						frameCounter = 1;
						runHzTotal = peakHzStorage[k];
					}
				}
#ifdef HDEBUG
				printf("partialTracks=%d\n", partialTrackCounter);
#endif
				thisIterationPeak.length = frameCounter;
				thisIterationPeak.peakHz = runHzTotal / frameCounter;
				thisIterationPeak.startFrame = startFrame;
				thisIterationPeak.endFrame = amountOfFrames - 1;
				partialTrackStorage[partialTrackCounter] = thisIterationPeak;
				partialTrackCounter += 1;

				int (*groupedFrequencyRuns)[2] = malloc(partialTrackCounter * sizeof(int[2]));
				int* partialTracksLeft = malloc(partialTrackCounter * sizeof(int));
				int numberOfTracksLeft = partialTrackCounter;

				if (groupedFrequencyRuns && partialTracksLeft)
				{
					for (int t = 0; t < partialTrackCounter; t++)
					{
						partialTracksLeft[t] = t;
					}

					while (numberOfTracksLeft > 0)
					{
						int targetTrack = partialTracksLeft[0];

						float startingTargetHz = partialTrackStorage[targetTrack].peakHz;
						int targetStartFrame = partialTrackStorage[targetTrack].startFrame;
						int targetEndFrame = partialTrackStorage[targetTrack].endFrame;

						int topScore = 0;
						int topPosition = -1;
						int topLength = 0;

						for (int i = 1; i < numberOfTracksLeft; i++)
						{
							int thisTrack = partialTracksLeft[i];
							int score = 0;
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
							if (targetStartFrame == partialTrackStorage[thisTrack].startFrame || targetEndFrame == partialTrackStorage[thisTrack].endFrame)
							{
								score += 1;
							}

							if (score > topScore)
							{
								topScore = score;
								topPosition = i;
								topLength = partialTrackStorage[thisTrack].length;
							}
							else if (score == topScore && score > 0 && partialTrackStorage[thisTrack].length > topLength)
							{
								topPosition = i;
								topLength = partialTrackStorage[thisTrack].length;
							}
						}

						groupedFrequencyRuns[groupCounter][0] = targetTrack;
						groupedFrequencyRuns[groupCounter][1] = topPosition >= 0 ? partialTracksLeft[topPosition] : -4;
						groupCounter += 1;

						if (topPosition >= 0)
						{
							for (int i = topPosition; i < numberOfTracksLeft - 1; i++)
							{
								partialTracksLeft[i] = partialTracksLeft[i + 1];
							}
							numberOfTracksLeft -= 1;
						}
						for (int i = 0; i < numberOfTracksLeft - 1; i++)
						{
							partialTracksLeft[i] = partialTracksLeft[i + 1];
						}
						numberOfTracksLeft -= 1;
#ifdef HDEBUG
						if (numberOfTracksLeft % 500 == 0) printf("grouping %d tracks left\n", numberOfTracksLeft);
#endif
					}
					free(partialTracksLeft);

					int groupStartFrame;
					int groupEndFrame;
					int totalEnvelopeLength = 0;
					int* groupStartFrames = malloc(groupCounter * sizeof(int));
					int* groupEndFrames = malloc(groupCounter * sizeof(int));

					if (groupStartFrames && groupEndFrames)
					{
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
								if (partialTrackStorage[targetRun1].endFrame > partialTrackStorage[targetRun2].endFrame)
								{
									groupEndFrame = partialTrackStorage[targetRun1].endFrame;
								}
								else
								{
									groupEndFrame = partialTrackStorage[targetRun2].endFrame;
								}
							}
							else
							{
								int targetRun = groupedFrequencyRuns[i][0];
								groupStartFrame = partialTrackStorage[targetRun].startFrame;
								groupEndFrame = partialTrackStorage[targetRun].endFrame;
							}
							groupStartFrames[i] = groupStartFrame;
							groupEndFrames[i] = groupEndFrame;
							totalEnvelopeLength += groupEndFrame - groupStartFrame + 1;
						}

						float* groupEnvelope = malloc(totalEnvelopeLength * sizeof(float));
						noteEvent* noteEventStorage = malloc(groupCounter * sizeof(noteEvent));

						if (groupEnvelope && noteEventStorage)
						{
							int envelopeOffset = 0;
							for (int i = 0; i < groupCounter; i++)
							{
								noteEvent thisNoteEvent;
								groupStartFrame = groupStartFrames[i];
								groupEndFrame = groupEndFrames[i];
								thisNoteEvent.startFrame = groupStartFrame;
								thisNoteEvent.endFrame = groupEndFrame;
								thisNoteEvent.envelopeLength = groupEndFrame - groupStartFrame + 1;
								if (groupedFrequencyRuns[i][1] != -4)
								{
									int targetRun1 = groupedFrequencyRuns[i][0];
									int targetRun2 = groupedFrequencyRuns[i][1];
									float frequency1 = partialTrackStorage[targetRun1].peakHz;
									float frequency2 = partialTrackStorage[targetRun2].peakHz;

									float lowHz = frequency1 < frequency2 ? frequency1 : frequency2;
									float highHz = frequency1 < frequency2 ? frequency2 : frequency1;
									float harmonicRatio = lowHz > 0.0f ? highHz / lowHz : 1.0f;
									int harmonicsCheck = 0;
									for (int m = 1; m < 9; m++)
									{
										if (fabsf(harmonicRatio - m) < 0.25f)
										{
											harmonicsCheck = 1;
										}
									}

									float run1MagTotal = 0;
									float run2MagTotal = 0;
									int run1Frames = 0;
									int run2Frames = 0;
									for (int s = groupStartFrame; s <= groupEndFrame; s++)
									{
										float frameMagnitude = 0;
										if (s >= partialTrackStorage[targetRun1].startFrame && s <= partialTrackStorage[targetRun1].endFrame)
										{
											int bin1 = hzToBin(partialTrackStorage[targetRun1].peakHz, frameSize, sampleRate, binSize);
											float thisMag = magnitudeStorage[s * binSize + bin1];
											frameMagnitude += thisMag;
											run1MagTotal += thisMag;
											run1Frames += 1;
										}
										if (s >= partialTrackStorage[targetRun2].startFrame && s <= partialTrackStorage[targetRun2].endFrame)
										{
											int bin2 = hzToBin(partialTrackStorage[targetRun2].peakHz, frameSize, sampleRate, binSize);
											float thisMag = magnitudeStorage[s * binSize + bin2];
											frameMagnitude += thisMag;
											run2MagTotal += thisMag;
											run2Frames += 1;
										}
										groupEnvelope[envelopeOffset + (s - groupStartFrame)] = frameMagnitude;
									}

									if (harmonicsCheck)
									{
										thisNoteEvent.fundamental = lowHz;
									}
									else
									{
										float run1Average = run1Frames > 0 ? run1MagTotal / run1Frames : 0.0f;
										float run2Average = run2Frames > 0 ? run2MagTotal / run2Frames : 0.0f;
										thisNoteEvent.fundamental = run1Average >= run2Average ? frequency1 : frequency2;
									}
								}
								else
								{
									int targetRun = groupedFrequencyRuns[i][0];
									thisNoteEvent.fundamental = partialTrackStorage[targetRun].peakHz;

									for (int s = groupStartFrame; s <= groupEndFrame; s++)
									{
										float frameMagnitude = 0;
										if (s >= partialTrackStorage[targetRun].startFrame && s <= partialTrackStorage[targetRun].endFrame)
										{
											int bin1 = hzToBin(partialTrackStorage[targetRun].peakHz, frameSize, sampleRate, binSize);
											frameMagnitude += magnitudeStorage[s * binSize + bin1];
										}
										groupEnvelope[envelopeOffset + (s - groupStartFrame)] = frameMagnitude;
									}
								}
								thisNoteEvent.envelope = &groupEnvelope[envelopeOffset];
								envelopeOffset += thisNoteEvent.envelopeLength;
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
					free(groupedFrequencyRuns);
				}
				else
				{
					free(groupedFrequencyRuns);
					free(partialTracksLeft);
				}
				free(partialTrackStorage);
			}
		}
		free(magnitudeStorage);
		free(peakHzStorage);
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
