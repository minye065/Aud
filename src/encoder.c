//encoder.c
#include <stdio.h>

char* writebuffer[];
char passedinsong; // TEMP

typedef struct sector
{
	unsigned char pitch;
	unsigned char volume;
	unsigned char duration;
}sector;

typedef struct song
{
	sector *writebuffer;
	unsigned int bufferelementcount;
}song;

song encode(char passedinsong)
{
	song final;
	//split song into sectors
	char sectors[100]; // TEMP
	int bufferelementcount = 0;
	for (int i = 0; i < sizeof(sectors); i++) // sectors can not be a pointer
	{

		bufferelementcount++;
	}
	final.bufferelementcount = bufferelementcount;
	final.writebuffer = 0; // TEMP
	return final;
}

int main()
{
	char targetfile[] = 0; // TEMP
	song targetsong = encode(passedinsong);
	FILE* ptargetfile = fopen(targetfile, "wbx");

	if (ptargetfile)
	{
		unsigned int bufferelementcount = targetsong.bufferelementcount;
		char* writebuffer = targetsong.writebuffer;
		size_t writebuffersize = sizeof(*writebuffer);

		fwrite(writebuffer, writebuffersize, bufferelementcount, ptargetfile);
		fclose(ptargetfile);
	}
}