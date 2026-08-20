#ifndef APP_H
#define APP_H
#define PI 3.14159265358979323846
#include <Windows.h>

typedef struct note
{
	float fundamental;
	int startFrame;
	int endFrame;
	float* envelope;
	int envelopeLength;
	float startTime;
	float endTime;
	float timeLength;
} note;

int noteOrdering(const void* a, const void* b);
float getActiveNotes(float currentTime);

#endif