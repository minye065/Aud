#ifndef APP_H
#define APP_H
#define PI 3.14159265358979323846
#include <Windows.h>
#include <stdbool.h>
#include <stdint.h>

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

int noteOrdering(const void* a, const void* b);
float getActiveNotes(song* currentSong, int16_t* returnSamples, int samplesNeeded, int64_t startSample);

#endif