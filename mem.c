/*
The MIT License (MIT)

Copyright (c) 2015 James Nuttall
Questions/comments? Email at:
MobileForensicsResearch@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
TO BUILD:
from Ubuntu 12.04 VM:
sudo apt-get install gcc-arm-linux-gnueabi

To build for arm device:
arm-linux-gnueabi-gcc mem.c -static -o mem

Now, copy mem to the Android device, chmod it to run, then run it over ADB

Yes, you can overflow the input buffers. The fix is: don't.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ptrace.h>
#include <errno.h>
#include <dirent.h>

// Enable for debug output todo
//#define DBGR 0

// save the memory from start-(start+length) of PID to the file 'name'
void readMem(int pid, unsigned int start, unsigned int len, FILE *out_file);
void pullPidMemory(FILE *out_file, int target_pid);
void pullAllPidMemory(FILE *out_file);

#define VERSION 1.0

void usage()
{
	printf("\nWelcome to mem, version %.1F\n", VERSION);
	printf("\nUsage:\n");
	printf("mem reads the memory from a given PID and writes to a file\n");
	printf(" - May require elevated permissions\n");
	printf("./mem <pid> <out_path>\n");
	printf(" - where <pid> is the target PID to capture\n");
	printf(" - and <out_path> is the local dir to write output\n");
	printf(" If <out_path> is not there, writes to stdout\n");
	printf(" If <pid> is 0, all PIDs will be captured\n\n\n");
}

// NOTE: This will require elevated permissions (because it is reading another process' memory)
int main(int argc, void **argv)
{
	if (argc < 2)
	{
		printf("Missing target PID\n\n");
		usage();
		return -1;
	}
	if (argc > 3)
	{
		usage();
		return -1;
	}

	char *outdir = calloc(1, 256);
	if(argc == 3)
	{
		// read in the ouput path
		strncpy(outdir, argv[2], 255);
	} 
	else 
	{
		// no output path given
		// we will write to stdout
		strcpy(outdir, "\0");
	}

	FILE * out_file;
	char *tmp_name = calloc(1,256);

	if (strlen(outdir) != 0) 
	{
		out_file = fopen(outdir, "w");
		printf("writing to: %s\n", outdir);
		if(out_file == NULL) {
			perror("Error opening output file.");
			exit(-1);
		}
	}
	else
	{
		//printf("writing to standard out");
		out_file = stdout;
	}

	int target_pid = atoi(argv[1]); // TODO: this will fail ungracefully if not given a number, strtol?
	if (target_pid == 0)
		// Capture all memory for all PIDs
		pullAllPidMemory(out_file);
	else
		// Capture memory for single PID
		pullPidMemory(out_file, target_pid);

	free(outdir);
	fclose(out_file);

	return 0;
}

void readMem(int pid, unsigned int start, unsigned int len, FILE *out_file)
{
	FILE *pid_mem_file;
	char *temp_buffer = calloc(1, len);
	char *tmp_name = calloc(1, 256);

	snprintf(tmp_name, 255, "/proc/%d/mem", pid);
	pid_mem_file = fopen(tmp_name, "r");
	if(pid_mem_file == NULL) {
		perror("Error opening mem for process.");
		exit(-1);
	}
	fseek(pid_mem_file, start, SEEK_SET);
	fread(temp_buffer, 1, len, pid_mem_file);

	fwrite(temp_buffer, 1, len, out_file);

	fclose(pid_mem_file);
	free(tmp_name);
	free(temp_buffer);
}

// given an output path and pid, pull a file, write to output
void pullPidMemory(FILE *out_file, int target_pid)
{
	FILE *mem_maps_file;
	
	char *maps_path = calloc(1, 256);
	char *name = calloc(1, 256);

	unsigned int mem_low = 0;
	unsigned int mem_high = 0;

	int i;

	errno = 0;
	i = ptrace(PTRACE_ATTACH, target_pid, 0, 0);
	if(i == -1 && errno)
	{
		#ifdef DBGR
		char tmp[80];
		sprintf(tmp, "Error attaching to this PID's process %d", target_pid);
		perror(tmp);
		#endif
		free(maps_path);
		free(name);
		return;
	}

	waitpid(target_pid, NULL, 0);

	snprintf(maps_path, 255, "/proc/%d/maps", target_pid);
	#ifdef DBGR
	printf("%s\n", maps_path);
	#endif
	mem_maps_file = fopen(maps_path, "r");
	if(mem_maps_file == NULL)
	{
		perror("Error opening memory map for this PID");
		goto leave;
	}

	// read the entire memory map
	while(!feof(mem_maps_file))
	{
		// retrieve the memory range and its label
		fscanf(mem_maps_file, "%x-%x %[^\n]", &mem_low, &mem_high, name);
		
		// we want this PID's heap 
		if(strstr(name, "[heap]") || strstr(name, "[stack]") || strstr(name, "deleted"))
		{
			readMem(target_pid, mem_low, mem_high-mem_low, out_file);
		}
	}
leave:
	ptrace(PTRACE_DETACH, target_pid, 0, 0);
	free(maps_path);
	free(name);
}

// given an ouptut path, pull all PID's memory
void pullAllPidMemory(FILE *out_file)
{
	DIR *d;
	struct dirent *dir;
	d = opendir("/proc/");

	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			long int val;
			char *unused;
			val = strtol(dir->d_name, &unused, 10);
			#ifdef DBGR
			printf("file descriptor: %lu\n", val);
			#endif
			if (val == 0)
			{
				// not a number, skip it
				// note, I don't care about PID 0 (kernel scheduler), so this is OK
				continue;
			}
			else
			{
				// this is a PID number, read its memory
				pullPidMemory(out_file, (int)val);
			}
		}
	}
}
