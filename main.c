//main.c
#include <stdio.h>

char targetfile[];
FILE* ptargetfile;
void* ptargetfilecontents;

typedef struct sector
{
	unsigned char pitch;
	unsigned char volume;
	unsigned char duration;
}sector;


int main()
{
	ptargetfile = fopen(targetfile, "r");

	if(ptargetfile)
	{
		fseek(ptargetfile, 0L, SEEK_END);
		int filesize = ftell(ptargetfile);
		rewind(ptargetfile);
		
		ptargetfilecontents = malloc(ptargetfile);

		fread(ptargetfilecontents, 1, filesize, ptargetfile);
		fclose(ptargetfile);
	}


}