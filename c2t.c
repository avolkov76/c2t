/*

c2t, Code to Tape|Text, Version 0.997, Wed Sep 27 15:27:56 GMT 2017

Parts copyright (c) 2011-2017 All Rights Reserved, Egan Ford (egan@sense.net)

THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY 
KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

Built on work by:
	* Mike Willegal (http://www.willegal.net/appleii/toaiff.c)
	* Paul Bourke (http://paulbourke.net/dataformats/audio/, AIFF and WAVE output code)
	* Malcolm Slaney and Ken Turkowski (Integer to IEEE 80-bit float code)
	* Lance Leventhal and Winthrop Saville (6502 Assembly Language Subroutines, CRC 6502 code)
    * Piotr Fusik (http://atariarea.krap.pl/x-asm/inflate.html, inflate 6502 code)
    * Rich Geldreich (http://code.google.com/p/miniz/, deflate C code)
    * Mike Chambers (http://rubbermallet.org/fake6502.c, 6502 simulator)

License:
	*  Do what you like, remember to credit all sources when using.

Description:
	This small utility will read Apple I/II binary and
	monitor text files and output Apple I or II AIFF and WAV
	audio files for use with the Apple I and II cassette
	interface.

Features:
	*  Apple I, II, II+, IIe support.
	*  Big and little-endian machine support.
		o  Little-endian tested.
	*  AIFF and WAVE output (both tested).
	*  Platforms tested:
		o  32-bit/64-bit x86 OS/X.
		o  32-bit/64-bit x86 Linux.
		o  32-bit x86 Windows/Cygwin.
		o  32-bit x86 Windows/MinGW.
	*  Multi-segment tapes.

Compile:
	OS/X:
		gcc -Wall -O -o c2t c2t.c
	Linux:
		gcc -Wall -O -o c2t c2t.c -lm
	Windows/Cygwin:
		gcc -Wall -O -o c2t c2t.c
	Windows/MinGW:
		PATH=C:\MinGW\bin;%PATH%
		gcc -Wall -O -static -o c2t c2t.c

Notes:
	*  Virtual ][ only supports .aif (or .cass)
	*  Dropbox only supports .wav and .aiff (do not use .wave or .aif)

Not yet done:
	*  Test big-endian.
	*  gnuindent

Thinking about:
	*  Check for existing file and abort, or warn, or prompt.
	*  -q quiet option for Makefiles
	*  autoload support for basic programs

Bugs:
	*  Probably

*/

#if defined(_WIN32)
#include "miniz_win32.h"
#else
#include "miniz.h"
#endif
#include <fake6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <c2t.h>

#define ABS(x) (((x) < 0) ? -(x) : (x))

#define VERSION "Version 0.997"
#define OUTFILE argv[argc-1]
#define BINARY 0
#define MONITOR 1
#define AIFF 2
#define WAVE 3
#define DSK 4 

typedef struct outbuf {
	double *sound;
	long length;
	long capacity;
	int offset;
	int rate;
} outbuf;

void usage(void);
char *getext(char *filename);
void outbuf_init(outbuf *buf, int rate);
void outbuf_reserve(outbuf *buf, long n);
void appendtone(outbuf *buf, int freq, double time, double cycles);
void Write_AIFF(FILE * fptr, double *samples, long nsamples, int nfreq, int bits, double amp);
void Write_WAVE(FILE * fptr, double *samples, long nsamples, int nfreq, int bits, double amp);
void ConvertToIeeeExtended(double num, unsigned char *bytes);
uint8_t read6502(uint16_t address);
void write6502(uint16_t address, uint8_t value);

unsigned char ram[65536];

typedef struct seg {
	int start;
	int length;
	int codelength;
	unsigned char *data;
	char filename[256];
} segment;

#define MAXEVENTS 100

typedef struct event {
	unsigned long int timestamp;
	char label[64];
} event;

unsigned int eventnumber = 0;

void registerevent(event *events, unsigned long int timestamp, const char *format, ...);
void printevents(event *events, int rate);

typedef struct s {
	unsigned char bytes[256];
} sector;

typedef struct t {
	sector sectors[16];
} track;

typedef struct d {
	track tracks[35];
} disk;

typedef struct tapegen tapegen;
struct tapegen {
	const char *const name;	// descriptive name
	const int bps;		// avg bits/second
	const int samp_rate;	// audio sampling rate required

	// all function pointers must be set
	void (* init_checksum)(tapegen *);
	void (* checksum_byte)(tapegen *, unsigned char b);
	double (* compute_length)(tapegen *, unsigned char *data, size_t datalen); // in seconds
	void (* write_preamble)(tapegen *, outbuf *buf, double extra /*seconds*/);
	void (* write_start)(tapegen *, outbuf *buf);
	void (* write_stop)(tapegen *, outbuf *buf);
	void (* write_byte)(tapegen *, outbuf *buf, unsigned char b);
	void (* write_checksum)(tapegen *, outbuf *buf);
	void (* write_filler)(tapegen *, outbuf *buf, double time /*seconds*/);

	// the following may or may not be set
	const double pre_len;	// standard preamble length in seconds
	const int freq_pre;	// preamble frequency used
	const int freq_end;	// ending frequency used
	const int freq_fill;	// filler frequency used (waits)
	const int freq0;	// frequency of bit 0 (FM coding)
	const int freq1;	// frequency of bit 1 (FM coding)

	unsigned char checksum;	// current running checksum

	float last_polarity;	// internal generator state
};

void gen_write_checked_byte(tapegen *gen, outbuf *buf, unsigned char b);
void gen_write_block(tapegen *gen, outbuf *buf, unsigned char *data, size_t datalen);
void gen_checksum_block(tapegen *gen, unsigned char *data, size_t datalen);

void gen_init_checksum(tapegen *gen);
void gen_checksum_byte(tapegen *gen, unsigned char b);
double fmgen_compute_length(tapegen *gen, unsigned char *data, size_t datalen);
void gen_write_preamble(tapegen *gen, outbuf *buf, double extra);
void null_write_start(tapegen *gen, outbuf *buf);
void a1tape_write_start(tapegen *gen, outbuf *buf);
void a2tape_write_start(tapegen *gen, outbuf *buf);
void fmgen_write_stop(tapegen *gen, outbuf *buf);
void a1tape_write_stop(tapegen *gen, outbuf *buf);
void fmgen_write_byte(tapegen *gen, outbuf *buf, unsigned char b);
void gen_write_checksum(tapegen *gen, outbuf *buf);
void null_write_checksum(tapegen *gen, outbuf *buf);
void gen_write_filler(tapegen *gen, outbuf *buf, double time);

tapegen a1tape = {
	"Apple Tape",
	1333 /*bps*/, 8000 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	fmgen_compute_length, gen_write_preamble,
	a1tape_write_start, a1tape_write_stop,
	fmgen_write_byte, null_write_checksum,
	gen_write_filler,
	4.0 /*pre_len*/, 1000 /*pre*/,
	1000 /*end*/, 1000 /*fill*/,
	2000 /*freq0*/, 1000 /*freq1*/,
};

tapegen a2tape = {
	"Apple II Tape",
	1333 /*bps*/, 11025 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	fmgen_compute_length, gen_write_preamble,
	a2tape_write_start, a1tape_write_stop,
	fmgen_write_byte, gen_write_checksum,
	gen_write_filler,
	4.0 /*pre_len*/, 770 /*pre*/,
	1000 /*end*/, 770 /*fill*/,
	2000 /*freq0*/, 1000 /*freq1*/,
};

// Symmetric FM 8000 bps coding; using fastload8000
tapegen sfm8000aud = {
	"SFM-8000",
	8000 /*bps*/, 48000 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	fmgen_compute_length, gen_write_preamble,
	null_write_start, fmgen_write_stop,
	fmgen_write_byte, gen_write_checksum,
	gen_write_filler,
	0.25 /*pre_len*/, 2000 /*pre*/,
	770 /*end*/, 2000 /*fill*/,
	12000 /*freq0*/, 6000 /*freq1*/,
};

void afm9600_write_byte(tapegen *gen, outbuf *buf, unsigned char b);
// Asymmetric FM 9600 bps coding (formerly 9600-hack); using fastload8000
tapegen afm9600aud = {
	"AFM-9600",
	9600 /*bps*/, 48000 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	fmgen_compute_length, gen_write_preamble,
	null_write_start, fmgen_write_stop,
	afm9600_write_byte, gen_write_checksum,
	gen_write_filler,
	0.25 /*pre_len*/, 2000 /*pre*/,
	770 /*end*/, 2000 /*fill*/,
	12000 /*freq0*/, 8000 /*freq1*/,
};

// Symmetric FM 9600 bps coding; using fastload9600
tapegen sfm9600aud = {
	"SFM-9600",
	9600 /*bps*/, 48000 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	fmgen_compute_length, gen_write_preamble,
	null_write_start, fmgen_write_stop,
	fmgen_write_byte, gen_write_checksum,
	gen_write_filler,
	0.25 /*pre_len*/, 6000 /*pre*/,
	2000 /*end*/, 6000 /*fill*/,
	12000 /*freq0*/, 8000 /*freq1*/,
};

// Symmetric FM 8820 bps coding; using fastloadcd
tapegen cd8820aud = {
	"SFM/CD-8820",
	8820 /*bps*/, 44100 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	fmgen_compute_length, gen_write_preamble,
	null_write_start, fmgen_write_stop,
	fmgen_write_byte, gen_write_checksum,
	gen_write_filler,
	0.25 /*pre_len*/, 5512 /*pre*/,
	2000 /*end*/, 5512 /*fill*/,
	11025 /*freq0*/, 7350 /*freq1*/,
};

double spm15k_compute_length(tapegen *gen, unsigned char *data, size_t datalen);
void spm15k_write_start(tapegen *gen, outbuf *buf);
void spm15k_write_stop(tapegen *gen, outbuf *buf);
void spm15k_write_byte(tapegen *gen, outbuf *buf, unsigned char b);
// Polarity-independent synchronous PM (phase-modulated) 15,360 bps coding; using fastload15k
tapegen spm15360aud = {
	"SPM/PI-15360",
	15360 /*bps*/, 48000 /*sampling rate*/,
	gen_init_checksum, gen_checksum_byte,
	spm15k_compute_length, gen_write_preamble,
	spm15k_write_start, spm15k_write_stop,
	spm15k_write_byte, gen_write_checksum,
	gen_write_filler,
	0.25 /*pre_len*/, 4000 /*pre*/,
	0 /*end*/, 4000 /*fill*/,
	19200 /*freq0*/, 19200 /*freq1*/,  // informational only
};

int square = 0;

int main(int argc, char **argv)
{
	FILE *ofp;
	outbuf buf;
	double amp=0.75;
	int i, c, model=0, outputtype, fileoutput=1, warm=0, dsk=0, noformat=0, k8=0, qr=0;
	int autoload=0, basicload=0, compress=0, fast=0, cd=0, tape=0, endpage=0, longmon=0, rate=11025, bits=8;
	char *filetypes[] = {"binary","monitor","aiff","wave","disk"};
	char *modeltypes[] = {"\b","I","II"};
	char *ext;
	unsigned int numseg = 0;
	segment *segments = NULL;
	event events[MAXEVENTS];

	opterr = 1;
	while((c = getopt(argc, argv, "12vabcftdpn8meh?lqr:")) != -1)
		switch(c) {
			case '1':		// apple 1
				rate = 8000;
				model = 1;
				break;
			case '2':		// apple 2
				model = 2;
				break;
			case 'v':		// version
				fprintf(stderr,"\n%s\n\n",VERSION);
				return 1;
				break;
			case 'a':		// assembly autoloader
				model = 2;
				autoload = 1;
				break;
			case 'b':		// basic autoloader
				model = 2;
				basicload = autoload = 1;
				break;
			case 'c':		// compression
				model = 2;
				autoload = compress = 1;
				break;
			case 'f':		// hifreq
				model = 2;
				autoload = fast = 1;
				cd = k8 = 0;
				break;
			case 'd':		// hifreq CD
				bits = 16;
				amp = 1.0;
				model = 2;
				cd = autoload = 1;
				fast = k8 = 0;
				break;
			case 't':		// 10 sec leader
				tape = 6;
				amp = 1.0;
				break;
			case 'm':		// drop to monitor after load
				warm = 1;
				break;
			case 'e':		// end on page boundary 
				endpage = 1;
				break;
			case 'p':		// stdout
				fileoutput = 0;
				break;
			case 'n':
				noformat = 1;
				break;
			case '8':		// 8k
				model = 2;
				autoload = k8 = 1;
				fast = cd = 0;
				break;
			case 'h':		// help
			case '?':
				usage();
				return 1;
			case 'q':		// qr code support
				model = 2;
				autoload = k8 = qr = 1;
				fast = cd = 0;
				break;
			case 'l':		// long mon lines
				longmon = 1;
				break;
			case 'r':		// override rate for -1/-2 only
				rate = atoi(optarg);
				autoload = basicload = k8 = qr = fast = cd = 0;
				break;
		}

	if(argc - optind < 1 + fileoutput) {
		usage();
		return 1;
	}

	// read input files

	fprintf(stderr,"\n");
	for(i=optind;i<argc-fileoutput;i++) {
		char start[5];
		unsigned char b, *data;
		int j, k, inputtype=BINARY;
		segment *tmp;
		FILE *ifp;

		if((tmp = realloc(segments, (numseg+1) * sizeof(segment))) == NULL) {
			fprintf(stderr,"could not allocate segment %d\n",numseg+1);
			abort();
		}
		segments = tmp;

		k=0;
		for(j=0;j<strlen(argv[i]);j++) {
			if(argv[i][j] == ',')
				break;
			segments[numseg].filename[k++]=argv[i][j];
		}
		segments[numseg].filename[k] = '\0';
		// TODO: store as basename, check for MINGW compat

		k=0;j++;
		for(;j<strlen(argv[i]);j++)
			start[k++]=argv[i][j];
		start[k] = '\0';
		if(k == 0)
			segments[numseg].start = -1;
		else
			segments[numseg].start = (int)strtol(start, (char **)NULL, 16);

		if((ext = getext(segments[numseg].filename)) != NULL)
			if(strcmp(ext,"mon") == 0)
				inputtype = MONITOR;

		// clean up later, just testing
		if((ext = getext(segments[numseg].filename)) != NULL) {
			if(strcmp(ext,"dsk") == 0)
				inputtype = DSK;
			if(strcmp(ext,"DSK") == 0)
				inputtype = DSK;
			if(strcmp(ext,"do") == 0)
				inputtype = DSK;
			if(strcmp(ext,"DO") == 0)
				inputtype = DSK;
			if(strcmp(ext,"po") == 0)
				inputtype = DSK;
			if(strcmp(ext,"PO") == 0)
				inputtype = DSK;
		}

		{
			const char* mode = "r";
			// Windows needs "b" for binary files; Linux/BSD will simply ignore "b" (see fopen(3))
			if(inputtype != MONITOR)
				mode = "rb";
			if ((ifp = fopen(segments[numseg].filename, mode)) == NULL) {
				fprintf(stderr,"Cannot read: %s\n\n",segments[numseg].filename);
				return 1;
			}
		}

		fprintf(stderr,"Reading %s, type %s, segment %d, start: ",segments[numseg].filename,filetypes[inputtype],numseg+1);

//hack to support dumping disks for testing, should be 48, not 140 (really should be dynamic)

		if((data = malloc(140*1024*sizeof(char))) == NULL) {
			fprintf(stderr,"could not allocate 140K data\n");
			abort();
		}

		if(inputtype == DSK) {
			disk floppy;
			int po = 0;
			unsigned int xref[]={0x0,0xE,0xD,0xC,0xB,0xA,0x9,0x8,0x7,0x6,0x5,0x4,0x3,0x2,0x1,0xF};

			// new version
			fread(&floppy, 143360, 1, ifp);
			//check feof/ferror
/*
			if(fread(&floppy, 1, 143360, ifp) != 143360) {
			{
				fprintf(stderr,"\n%s length != 143360 for file type DISK\n\n", segments[numseg].filename);
				return 1;
			}
*/

			// hack, just testing
			ext = getext(segments[numseg].filename);
			if(strcmp(ext,"po") == 0 || strcmp(ext,"PO") == 0)
				po = 1;

			dsk = 1;
			segments[numseg].length = 0;
			for(i=0;i<5;i++) {
				int j, k, l;

				//segments[numseg].start=i*(140 * 1024 / 5);
				segments[numseg].start = diskload2_data;

/* old version			
				while(fread(&b, 1, 1, ifp) == 1 && segments[numseg].length < (140 * 1024 / 5))
					data[segments[numseg].length++]=b;
*/
				// new version
				for(j=numseg*7;j<(numseg*7+7);j++)
					for(k=0;k<16;k++)
						for(l=0;l<256;l++)
							if(po)
								data[segments[numseg].length++]=floppy.tracks[j].sectors[xref[k]].bytes[l];
							else
								data[segments[numseg].length++]=floppy.tracks[j].sectors[k].bytes[l];

				segments[numseg].data = data;
				fprintf(stderr,"0x%04X, length: %d\n",segments[numseg].start,segments[numseg].length);

				if(segments[numseg].length != (140 * 1024 / 5)) {
					fprintf(stderr,"\n%s segment too short (< %d) for file type DISK\n\n",segments[numseg].filename,140*1024/5);
					return 1;
				}

				if(i==4)
					break;

				numseg++;
				if((tmp = realloc(segments, (numseg+1) * sizeof(segment))) == NULL) {
					fprintf(stderr,"could not allocate segment %d\n",numseg+1);
					abort();
				}
				segments = tmp;
				strcpy(segments[numseg].filename,segments[numseg-1].filename);
				segments[numseg].length = 0;
				if((data = malloc(48*1024*sizeof(char))) == NULL) {
					fprintf(stderr,"could not allocate 48K data\n");
					abort();
				}
				// old version
				//data[segments[numseg].length++]=b;

				fprintf(stderr,"Reading %s, type %s, segment %d, start: ",segments[numseg].filename,filetypes[inputtype],numseg+1);
			}
		}

		if(inputtype == BINARY) {
			if(segments[numseg].start == -1) {
				fread(&b, 1, 1, ifp);
				segments[numseg].start = b;
				fread(&b, 1, 1, ifp);
				segments[numseg].start |= b << 8;
				fread(&b, 1, 1, ifp);
				segments[numseg].length = b;
				fread(&b, 1, 1, ifp);
				segments[numseg].length |= b << 8;
			}

			segments[numseg].length=0;
			while(fread(&b, 1, 1, ifp) == 1)
				data[segments[numseg].length++]=b;

			segments[numseg].data = data;
			fprintf(stderr,"0x%04X, length: %d\n",segments[numseg].start,segments[numseg].length);
		}

		if(inputtype == MONITOR) {
			int byte, naddr;
			char addrs[8], s;

			segments[numseg].start = -1;
			segments[numseg].length = 0;

			while(fscanf(ifp,"%s ",addrs) != EOF) {
				naddr = (int)strtol(addrs, (char **)NULL, 16);
				if(segments[numseg].start == -1)
					segments[numseg].start = naddr;
	
				if(naddr != segments[numseg].start + segments[numseg].length) { // multi segment
					segments[numseg].data = data;
					fprintf(stderr,"0x%04X, length: %d\n",segments[numseg].start,segments[numseg].length);
					numseg++;
					if((tmp = realloc(segments, (numseg+1) * sizeof(segment))) == NULL) {
						fprintf(stderr,"could not allocate segment %d\n",numseg+1);
						abort();
					}
					segments = tmp;
					if((data = malloc(48*1024*sizeof(char))) == NULL) {
						fprintf(stderr,"could not allocate 48K data\n");
						abort();
					}
					segments[numseg].start = naddr;
					segments[numseg].length = 0;
					strcpy(segments[numseg].filename,segments[numseg-1].filename);
					fprintf(stderr,"Reading %s, type %s, segment %d, start: ",segments[numseg].filename,filetypes[inputtype],numseg+1);
				}
	
				while (fscanf(ifp, "%x%c", &byte, &s) != EOF) {
					data[segments[numseg].length++]=byte;
					if (s == '\n' || s == '\r')
						break;
				}
			}
			segments[numseg].data = data;
			fprintf(stderr,"0x%04X, length: %d\n",segments[numseg].start,segments[numseg].length);
		}

		fclose(ifp);
		numseg++;
	}
	fprintf(stderr,"\n");

	if(dsk) {
		fast=autoload=cd=tape=0;
		model=2;

		if(numseg != 5) {
			fprintf(stderr,"Number of segments != 5 and/or not of length %d\n\n",140*1024/5);
			return 1;
		}
		else {
			for(i=0;i<5;i++) {
				if(segments[i].length != 140*1024/5) {
					fprintf(stderr,"Number of segments != 5 and/or not of length %d\n\n",140*1024/5);
					return 1;
				}
			}	
		}
	}

	if(endpage)
		for(i=0;i<numseg;i++) {
			int pad = (0xFF - ((segments[i].length + segments[i].start - 1) & 0xFF));

			segments[i].length += pad;
			while(pad--)
				segments[i].data[segments[i].length - pad - 1] = 0;
		}

	if(numseg > 1 || model == 1) {
		if(autoload)
			fprintf(stderr,"WARNING: number of segments > 1 or model = 1: autoload and fast disabled.\n\n");
		autoload = fast = 0;
	}

	if(fileoutput) {
		if((ext = getext(OUTFILE)) == NULL) {
			usage();
			return 1;
		}
		else {
			if(strcmp(ext,"aiff") == 0 || strcmp(ext,"aif") == 0)
				outputtype = AIFF;
			else if(strcmp(ext,"wave") == 0 || strcmp(ext,"wav") == 0)
				outputtype = WAVE;
			else if(strcmp(ext,"mon") == 0)
				outputtype = MONITOR;
			else {
				usage();
				return 1;
			}
		}
	}
	else {
/*
		if(!model)
			outputtype = MONITOR;
		else
			outputtype = AIFF;
*/
		outputtype = MONITOR;
	}

	if(outputtype != MONITOR && !model) {
		fprintf(stderr,"\nYou must specify -1 or -2 for Apple I or II tape format, exiting.\n\n");
		return 1;
	}

	// TODO: check for existing file and abort, or warn, or prompt

	ofp=stdout;
	if(fileoutput) {
		const char* mode = "w";
		// Windows needs "b" for binary files; Linux/BSD will simply ignore "b" (see fopen(3))
		if(outputtype == AIFF || outputtype == WAVE)
			mode = "wb";
		if ((ofp = fopen(OUTFILE, mode)) == NULL) {
			fprintf(stderr,"\nCannot write: %s\n\n",OUTFILE);
			return 1;
		}
		fprintf(stderr,"Writing %s as Apple %s formatted %s.\n\n",OUTFILE,modeltypes[model],filetypes[outputtype]);
	}
	else
		fprintf(stderr,"Writing %s as Apple %s formatted %s.\n\n","STDOUT",modeltypes[model],filetypes[outputtype]);

	if(outputtype == MONITOR) {
		int i, j, saddr;
		// unsigned long cmp_len;
		size_t cmp_len;
		unsigned char *cmp_data;

		for(i=0;i<numseg;i++) {
			if(compress) {
				cmp_data = tdefl_compress_mem_to_heap(segments[i].data, segments[i].length, &cmp_len, TDEFL_MAX_PROBES_MASK);
				free(segments[i].data);
				segments[i].data = cmp_data;
				segments[i].length = cmp_len;
			}
			saddr = segments[i].start;
			fprintf(ofp,"%04X:", saddr);
			for(j=0;j<segments[i].length;j++) {
				fprintf(ofp," %02X", segments[i].data[j]);
				if(++saddr % (8+(24*longmon)) == 0 && j < segments[i].length - 1)
					fprintf(ofp,"\n%04X:",saddr);
			}
			fprintf(ofp,"\n");
		}

		fclose(ofp);
		return 0;
	}

	// write out code
	if(!autoload  && !dsk) {
		int i;
		size_t cmp_len;
		unsigned char *cmp_data;
		tapegen *gen;

		if(model == 1)
			gen = &a1tape;
		else /* model == 2 */
			gen = &a2tape;

		outbuf_init(&buf, rate);

		for(i=0;i<numseg;i++) {
			gen->write_preamble(gen, &buf, tape);
			gen->write_start(gen, &buf);
			gen->init_checksum(gen);

			if(compress) {
				cmp_data = tdefl_compress_mem_to_heap(segments[i].data, segments[i].length, &cmp_len, TDEFL_MAX_PROBES_MASK);
				free(segments[i].data);
				segments[i].data = cmp_data;
				segments[i].length = cmp_len;
			}

			gen_write_block(gen, &buf, segments[i].data, segments[i].length);
			
			// checksum/endbits
			gen->write_checksum(gen, &buf);
			gen->write_stop(gen, &buf);
		}

		// friendly help
		fprintf(stderr,"To load up and run on your Apple %s, type:\n\n",modeltypes[model]);
		if(model == 1)
			fprintf(stderr,"\tC100R\n\t");
		else
			fprintf(stderr,"\tCALL -151\n\t");

		for(i=0;i<numseg;i++)
			fprintf(stderr,"%X.%XR ",segments[i].start,segments[i].start+segments[i].length-1);
		fprintf(stderr,"\n");

		if(numseg == 1) {
			if(model == 1)
				fprintf(stderr,"\t%XR\n",segments[0].start);
			else
				fprintf(stderr,"\t%XG\n",segments[0].start);
		}
		fprintf(stderr,"\n");
	}

	if(autoload) {
		double eta = 0;
		char loading[100]; // "\rLOADING ...";
		size_t nameavail;
		unsigned char *cmp_data, table[12];
		unsigned char *autoloadcode;
		size_t autoloadcode_len;
		size_t cmp_len;
		unsigned int length;
		int i, j;
		tapegen *gen, *basgen = &a2tape;

		if(fast) {
			// synchronous PM 15k variant
			autoloadcode = fastload15k;
			autoloadcode_len = sizeof(fastload15k)/sizeof(char);
			gen = &spm15360aud;
		}
		else if(fast && 0 /*disabled*/) {
			// asymmetric 9600 variant; not entirely stable
			autoloadcode = fastload8000;
			autoloadcode_len = sizeof(fastload8000)/sizeof(char);
			gen = &afm9600aud;
			square = 1;
		}
		else if(k8) {
			autoloadcode = fastload8000;
			autoloadcode_len = sizeof(fastload8000)/sizeof(char);
			gen = &sfm8000aud;
		}
		else if(cd) {
			autoloadcode = fastloadcd;
			autoloadcode_len = sizeof(fastloadcd)/sizeof(char);
			gen = &cd8820aud;
		}
		else /* 1333 autoload */ {
			autoloadcode = autoload1333;
			autoloadcode_len = sizeof(autoload1333)/sizeof(char);
			gen = &a2tape;
		}

		outbuf_init(&buf, gen->samp_rate);

		// compute uncompressed ETA
		eta = gen->compute_length(gen, segments[0].data, segments[0].length);

		if(compress) {
			double cmp_eta = 0;
			double inflate_time = 0;
			const unsigned int simaddr = 0xBF00;

			cmp_data = tdefl_compress_mem_to_heap(segments[0].data, segments[0].length, &cmp_len, TDEFL_MAX_PROBES_MASK);

			cmp_eta = gen->compute_length(gen, cmp_data, cmp_len);
			// we need to append inflate/decompress code to end of data
			cmp_eta += gen->compute_length(gen, inflatecode, sizeof(inflatecode)/sizeof(char));

			//compute inflate time
			const unsigned int dataorg = autoload3_org - cmp_len;
			//load up inflate data
			gen->init_checksum(gen);
			memcpy(ram + dataorg, cmp_data, cmp_len*sizeof(char));
			gen_checksum_block(gen, cmp_data, cmp_len);
			//load up inflate code
			memcpy(ram + autoload3_org, inflatecode, sizeof(inflatecode));
			gen_checksum_block(gen, inflatecode, sizeof(inflatecode)/sizeof(char));
			// append checksum to loaded data and code
			ram[autoload3_org + sizeof(inflatecode)/sizeof(char)] = gen->checksum;

			//zero page src
			ram[autoload3_zp + 0] = dataorg & 0xFF;
			ram[autoload3_zp + 1] = dataorg >> 8;
			//zero page dst
			ram[autoload3_zp + 2] = (segments[0].start) & 0xFF; 
			ram[autoload3_zp + 3] = (segments[0].start) >> 8;
			//setup JSR
			ram[simaddr + 0] = 0x20; // JSR autoload3 inflate
			ram[simaddr + 1] = autoload3_org & 0xFF;
			ram[simaddr + 2] = autoload3_org >> 8;
			ram[simaddr + 3] = 0x00; //BRK to stop simulation
			//run it
			reset6502();
			exec6502(simaddr);
			//compare (just to be safe)
			for(j=0;j<segments[0].length;j++)
				if(ram[segments[0].start + j] != segments[0].data[j]) {
					fprintf(stderr,"WARNING: simulated inflate failed at %04X\n",segments[0].start+j);
					break;
				}
			inflate_time += clockticks6502/1023000.0;

			fprintf(stderr,"start: 0x%04X, length: %5d, deflated: %.02f%%, data time:%.02f, inflate time:%.02f\n",dataorg,(unsigned int)cmp_len,100.0*(1-cmp_len/(float)segments[0].length),cmp_eta,inflate_time);

			if(eta < inflate_time + cmp_eta) {
				fprintf(stderr,"WARNING: compression disabled: no significant gain (%.02f)\n",eta);
				compress = 0;
			}
			else {
				free(segments[0].data);
				segments[0].data = cmp_data;
				segments[0].codelength = segments[0].length;
				segments[0].length = cmp_len;
				eta = cmp_eta;
			}
			fprintf(stderr,"\n");
		}

		eta += gen->pre_len;
		// calculate space available for filename
		sprintf(loading,"\rLOADING %s, ETA %d SEC. ","",(int)(eta+0.5));
		nameavail = autoload_mlen - 1 - strlen(loading);
		if(nameavail < strlen(segments[0].filename)) {
			segments[0].filename[nameavail] = '\0';
			fprintf(stderr,"WARNING: Loading message buffer overflow: truncating display filename to %s\n\n",segments[0].filename);
		}
		sprintf(loading,"\rLOADING %s, ETA %d SEC. ",segments[0].filename,(int)(eta+0.5));
		// post-process the LOADING message
		for(i=0;i<strlen(loading);i++) {
			if(loading[i] == '_')
				loading[i] = ' ';
			else
				loading[i] = toupper(loading[i]);
		}

		length = sizeof(basic)/sizeof(char) + autoloadcode_len + sizeof(table)/sizeof(char);

		// write out the bootstrap code, BASIC or asm
		basgen->write_preamble(basgen, &buf, tape);
		basgen->write_start(basgen, &buf);
		basgen->init_checksum(basgen);

		if(basicload) { // write basic stub
			// first the standard header
			header[0] = length & 0xFF;
			header[1] = length >> 8;
			gen_write_block(basgen, &buf, header, 3);
			basgen->write_checksum(basgen, &buf);
			basgen->write_stop(basgen, &buf);

			// write out basic program (needs another preamble+start sequence)
			basgen->write_preamble(basgen, &buf, 0);
			basgen->write_start(basgen, &buf);
			basgen->init_checksum(basgen);
			gen_write_block(basgen, &buf, basic, sizeof(basic)/sizeof(char));
		}
		else { // write out JMP 80C NOP NOP ...
			unsigned char patch[] = {0x4C,0x0C,0x08,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA};
			// no header for an asm load
			gen_write_block(basgen, &buf, patch, sizeof(patch)/sizeof(char));
		}

		// write out move and load code
		if(compress) {
			unsigned int cmp_start = autoload3_org - segments[0].length;

			//load start
			table[0] = cmp_start & 0xff;
			table[1] = cmp_start >> 8;

			//load end
			table[2] = (cmp_start + segments[0].length + sizeof(inflatecode)/sizeof(char) + 1) & 0xff;
			table[3] = (cmp_start + segments[0].length + sizeof(inflatecode)/sizeof(char) + 1) >> 8;

			//inflate src
			table[4] = cmp_start & 0xff;
			table[5] = cmp_start >> 8;

			//inflate end
			table[8] = (segments[0].start + segments[0].codelength) & 0xff;
			table[9] = (segments[0].start + segments[0].codelength) >> 8;
		}
		else {
			//load start
			table[0] = segments[0].start & 0xff;
			table[1] = segments[0].start >> 8;

			//load end
			table[2] = (segments[0].start + segments[0].length + 1) & 0xff;
			table[3] = (segments[0].start + segments[0].length + 1) >> 8;
		}
		//JMP to code, inflate dst
		table[6] = segments[0].start & 0xff;
		table[7] = segments[0].start >> 8;
		table[10] = compress;
		table[11] = warm;

		// patch in LOADING message
		for(i=0;i<strlen(loading);i++)
			autoloadcode[autoload_msg - 0x80C + i] = loading[i]; // | 0x80 ?

		// write out autoload code
		gen_write_block(basgen, &buf, autoloadcode, autoloadcode_len);
		// append table
		gen_write_block(basgen, &buf, table, sizeof(table)/sizeof(char));
		// it's a wrap!
		gen_write_checked_byte(basgen, &buf, 0xff);

		if(!basicload) {
			// pad all autoload objects to $200 in low mem; same load syntax
			int pad = 0x1ff - length;
			length += pad;
			while(pad--)
				gen_write_checked_byte(basgen, &buf, 0x00);
		}

		basgen->write_checksum(basgen, &buf);
		basgen->write_stop(basgen, &buf);

		if(qr) {
			// (!) Full reset: remove all audio so far (bootstrapper, etc.) and start anew
			buf.length = 0;

			// 0.25 sec
			gen->write_preamble(gen, &buf, 0);
			gen->write_start(gen, &buf);
			gen->init_checksum(gen);

			// parameters, 12 bytes
			gen_write_block(gen, &buf, table, sizeof(table)/sizeof(char));

			// LOADING message
			for(i=1;i<strlen(loading);i++) {
				// XXX: Is $80 char flag necessary?
				gen_write_checked_byte(gen, &buf, loading[i] | 0x80);
			}

			for(i=0;i<60-strlen(loading+1);i++) {
				gen_write_checked_byte(gen, &buf, 0x00);
			}

			gen->write_checksum(gen, &buf);
			// end of parameters
			gen->write_stop(gen, &buf);

			// time is needed to process the params; the next preamble takes care of that
		}

		// now the code
		gen->write_preamble(gen, &buf, 0);
		gen->write_start(gen, &buf);
		gen->init_checksum(gen);

		gen_write_block(gen, &buf, segments[0].data, segments[0].length);

		if(compress) {
			// need the inflate code to decompress data
			gen_write_block(gen, &buf, inflatecode, sizeof(inflatecode)/sizeof(char));
		}

		// XXX: ???
		if(fast + cd + k8 == 0) {	// hack so that standard method matches others
			gen_write_checked_byte(gen, &buf, 0x00);
			gen_write_checked_byte(gen, &buf, 0x00);
		}

		gen->write_checksum(gen, &buf);
		gen->write_stop(gen, &buf);
		gen->write_filler(gen, &buf, 0.01);

		if(!qr) {
			if(basicload) {
				fprintf(stderr,"To load up and run on your Apple %s, type:\n\n\tLOAD\n",modeltypes[model]);
				if(warm)
					fprintf(stderr,"\t%XG\n",segments[0].start);
			}
			else {
				fprintf(stderr,"To load up and run on your Apple %s, type:\n\n\t800.%XR 800G\n",modeltypes[model],0x800 + length + 1);
			}
		}
		else {
			fprintf(stderr,"To load up and run on your Apple %s, use the client disk.\n",modeltypes[model]);
		}
		fprintf(stderr,"\n");
	}

	if(dsk) {
		double eta=0;
		char loading[60]; // "LOADING ...";
		unsigned char *cmp_data, start_table[21], *diskloadcode;
		unsigned long diskloadcode_len;
		size_t cmp_len;
		unsigned int length, start_table_len = 0;
		int i, j;
		tapegen *gen, *basgen = &a2tape;
		double inflate_times[5];
		double total_data_time = 0, total_inflate_time = 0;

		if(k8) {
			diskloadcode = diskload8000;
			diskloadcode_len = sizeof(diskload8000)/sizeof(char);
			gen = &sfm8000aud;
		}
		else if(0 /*disabled*/) {
			// symmetric 9600 variant; unstable
			diskloadcode = diskload9600;
			diskloadcode_len = sizeof(diskload9600)/sizeof(char);
			gen = &sfm9600aud;
		}
		else {	// default
			// asymmetric 9600 variant; not entirely stable
			diskloadcode = diskload8000;
			diskloadcode_len = sizeof(diskload8000)/sizeof(char);
			gen = &afm9600aud;
			square = 1;
		}

		outbuf_init(&buf, gen->samp_rate);

		// compute ETA
		eta = gen->compute_length(gen, diskloadcode2, sizeof(diskloadcode2)/sizeof(char));
		// compute pad length; pad to end of last page
		// XXX: The trailing infdata table is not ready yet. Just use random diskload bits -- good enough for time estimates
		eta += gen->compute_length(gen, diskloadcode2, ((sizeof(diskloadcode2)/sizeof(char) + 0xFF) & 0xFF00) - sizeof(diskloadcode2)/sizeof(char));
		eta += gen->compute_length(gen, diskloadcode3, sizeof(diskloadcode3)/sizeof(char));
		eta += gen->compute_length(gen, dosrwts, sizeof(dosrwts)/sizeof(char));
		eta += gen->pre_len;

		// generate the LOADING message
		sprintf(loading,"LOADING INSTA-DISK, ETA %d SEC. ",(int)(eta+0.5));
		if(strlen(loading)+1 > diskload1_mlen) {
			// this should never happen, but..
			loading[diskload1_mlen-1] = '\0';
			fprintf(stderr,"WARNING: Loading message buffer overflow: truncating message\n\n");
		}

		// write out BASIC stub header
		registerevent(events,buf.length,"%dHz Preamble + Sync Bit",basgen->freq_pre);

		basgen->write_preamble(basgen, &buf, tape);
		basgen->write_start(basgen, &buf);
		basgen->init_checksum(basgen);

		length = sizeof(basic)/sizeof(char) + diskloadcode_len;
		header[0] = length & 0xFF;
		header[1] = length >> 8;

		registerevent(events,buf.length,"BASIC Header + %dHz Preamble",basgen->freq_pre);

		gen_write_block(basgen, &buf, header, 3);
		basgen->write_checksum(basgen, &buf);
		basgen->write_stop(basgen, &buf);

		// actual BASIC stub and diskload asm code
		basgen->write_preamble(basgen, &buf, 0);
		basgen->write_start(basgen, &buf);
		basgen->init_checksum(basgen);

		registerevent(events,buf.length,"BASIC Stub/Assembly Code @ %d BPS",basgen->bps);

		// write out basic program
		gen_write_block(basgen, &buf, basic, sizeof(basic)/sizeof(char));

		// patch in LOADING message
		for(i=0;i<strlen(loading);i++)
			diskloadcode[diskload1_msg - 0x80C + i] = loading[i]; // | 0x80 ?

		// write out move and load code
		gen_write_block(basgen, &buf, diskloadcode, diskloadcode_len);

		// end of basic and diskloadcode
		gen_write_checked_byte(basgen, &buf, 0xff);
		basgen->write_checksum(basgen, &buf);
		basgen->write_stop(basgen, &buf);

		registerevent(events,buf.length,"INSTA-DISK Code + DOS Load @ %d BPS",gen->bps);

		// time to compress and compute start location and length
		// patch loadcode2 with start locations and ETA
		for(i=0;i<numseg;i++) {
			int err;
			double cmp_eta = 0;
			char eta[10];
			double orig_len;
			const unsigned int dataend = diskload1_org;  // cmp data loaded just below diskload1 object
			const unsigned int datachkaddr = dataend - 1; // loaded chksum location

			inflate_times[i] = 0;
			
			cmp_data = tdefl_compress_mem_to_heap(segments[i].data, segments[i].length, &cmp_len, TDEFL_MAX_PROBES_MASK);

			gen->init_checksum(gen);
			//compute inflate time
			const unsigned int dataorg = datachkaddr - cmp_len;
			memcpy(ram + diskload3_org, diskloadcode3, sizeof(diskloadcode3));
			//load up inflate data
			memcpy(ram + dataorg, cmp_data, cmp_len*sizeof(char));
			gen_checksum_block(gen, cmp_data, cmp_len);
			ram[dataorg + cmp_len] = gen->checksum;

			//zero page src
			ram[diskload3_zp + 0] = dataorg & 0xFF;
			ram[diskload3_zp + 1] = dataorg >> 8;
			//zero page dst
			ram[diskload3_zp + 2] = diskload2_data & 0xFF;
			ram[diskload3_zp + 3] = diskload2_data >> 8;
			//setup JSR (overwrites diskload1 object which is no longer needed)
			ram[diskload1_org + 0] = 0x20; // JSR diskload3 inflate
			ram[diskload1_org + 1] = diskload3_org & 0xFF;
			ram[diskload1_org + 2] = diskload3_org >> 8;
			ram[diskload1_org + 3] = 0x00; //BRK to stop simulation
			//run it
			reset6502();
			exec6502(diskload1_org);
			//compare (just to be safe)
			err=0;
			for(j=0;j<7 * 4096;j++)
				if(ram[diskload2_data + j] != segments[i].data[j]) {
					err = 1;
					break;
				}
			if(err)
				fprintf(stderr,"WARNING: simulated inflate failed at %04X\n",diskload2_data+j);
			inflate_times[i] += clockticks6502/1023000.0;

			free(segments[i].data);
			segments[i].data = cmp_data;
			orig_len = segments[i].length;
			segments[i].length = cmp_len;
			segments[i].start = dataorg;

			// compress ?
			// need to see what is faster, defaulting to compress for now
			// if not compressed do not set start location, change asm code to check for 0,0
			// and not use inflate code

			// where to load data
			start_table[start_table_len++] = segments[i].start & 0xFF;
			start_table[start_table_len++] = segments[i].start >> 8;

			// compressed data ETA
			cmp_eta = gen->compute_length(gen, segments[i].data, segments[i].length);
			cmp_eta += gen->pre_len;
			sprintf(eta,"%d",(int)(cmp_eta+0.5));

			// ETA
			start_table[start_table_len++] = eta[0] + 0x80;
			if(eta[1] != 0)
				start_table[start_table_len++] = eta[1] + 0x80;
			else
				start_table[start_table_len++] = 0;

			fprintf(stderr,"Segment: %d, start: 0x%04X, length: %5d, deflated: %.02f%%, data time: %5.02f, inflate time: %5.02f\n",i,segments[i].start,segments[i].length,100.0*(1-segments[i].length/orig_len),cmp_eta,inflate_times[i]);
			total_data_time += cmp_eta;
			total_inflate_time += inflate_times[i];
		}
		fprintf(stderr,"\n");

		// now the stage 2 code: INSTA-DISK, inflate and DOS
		gen->write_preamble(gen, &buf, 0);
		gen->write_start(gen, &buf);
		gen->init_checksum(gen);

		gen_write_block(gen, &buf, diskloadcode2, sizeof(diskloadcode2)/sizeof(char));

		start_table[start_table_len++] = noformat;
		gen_write_block(gen, &buf, start_table, start_table_len);

		// pad diskload2 to the end of last page
		const int diskload2pad = ((sizeof(diskloadcode2)/sizeof(char) + start_table_len + 0xFF) & 0xFF00) - (sizeof(diskloadcode2)/sizeof(char) + start_table_len);
		for(i=0;i<diskload2pad;i++) {
			gen_write_checked_byte(gen, &buf, 0x00);
		}

		gen_write_block(gen, &buf, diskloadcode3, sizeof(diskloadcode3)/sizeof(char));
		gen_write_block(gen, &buf, dosrwts, sizeof(dosrwts)/sizeof(char));

		gen->write_checksum(gen, &buf);
		gen->write_stop(gen, &buf);
		gen->write_filler(gen, &buf, 0.1);

		for(i=0;i<numseg;i++) {
//timing
			j=0;
			if(i>0) {
				//j = 6 + ceil(inflate_times[i-1]);  // 6 = write track time, may need to make it 7
				// disk ][ verified (format and no-format)
				// Virtual ][ emulator verified (format and no-format, 8K only)
				// CFFA3000 3.1 failed, needs more time

				j = ceil(6.5 + inflate_times[i-1]);  // 6 = write track time, may need to make it 7
				// disk ][ verified (format and no-format)
				// Apple duodisk verified (format and no-format)
				// CFFA3000 3.1 verified with USB stick (no-format only)
				// CFFA3000 3.1 failed with IBM 4GB Microdrive (too slow)
				// Nishida Radio SDISK // (no-format only)

				registerevent(events,buf.length,"Inflate + Write Delay (%d Hz)",gen->freq_fill);
			}
			if(i==1) {
				j+=2; // seek time for track 0, just in case
				if (!noformat) {
					registerevent(events,buf.length,"Format Track 0 Delay (%d Hz)",gen->freq_fill);
					j+=3; // track 0 format time; determines inter-sector padding
				}
			}

/* count down code
			for(;j>=0;j--) {
				checksum = 0xff;
				WRITEBYTE(j/10 + 48 + 0x80);
				checksum ^= (j/10 + 48 + 0x80);
				WRITEBYTE(j%10 + 48 + 0x80);
				checksum ^= (j%10 + 48 + 0x80);
				WRITEBYTE(0x00);
				checksum ^= 0x00;
				WRITEBYTE(checksum);
				appendtone(&buf,2000,0,1);
				appendtone(&buf,6000,1,0);
			}
*/

			// processing delay filler
			gen->write_filler(gen, &buf, j);

			registerevent(events,buf.length,"Load Segment @ %d BPS",gen->bps);

			gen->write_preamble(gen, &buf, 0);
			gen->write_start(gen, &buf);
			gen->init_checksum(gen);
			gen_write_block(gen, &buf, segments[i].data, segments[i].length);
			gen->write_checksum(gen, &buf);
			gen->write_stop(gen, &buf);
			gen->write_filler(gen, &buf, 0.01);
		}
		fprintf(stderr,"Times: Data: %f, Inflate: %f, Audio: %f, File: %s\n\n",total_data_time,total_inflate_time,buf.length/(float)buf.rate,segments[0].filename);

		registerevent(events,buf.length,"Inflate + Exit");
		printevents(events,buf.rate);

		fprintf(stderr,"To load up and run on your Apple %s, type:\n\n\tLOAD\n\n",modeltypes[model]);
	}

	// append zero to zero out last wave
	appendtone(&buf,0,0,1);

	// 0.1 sec quiet to help some emulators
	appendtone(&buf,0,0.1,0);

	// 0.4 sec quiet to help some IIs
	// appendtone(&buf,0,0.4,0);

	// write it
	if(outputtype == AIFF)
		Write_AIFF(ofp,buf.sound,buf.length,buf.rate,bits,amp);
	else if(outputtype == WAVE)
		Write_WAVE(ofp,buf.sound,buf.length,buf.rate,bits,amp);

	fclose(ofp);
	return 0;
}

void outbuf_init(outbuf *buf, int rate)
{
	buf->capacity = 65536;
	buf->sound = (double *)malloc(buf->capacity * sizeof(double));
	buf->length = 0;
	buf->offset = 0;
	buf->rate = rate;
}

// ensure there is enough space to store n samples; grow sound buffer if necessary
void outbuf_reserve(outbuf *buf, long n)
{
	// grow capacity of buffer if needed, using size-doubling approach
	if(buf->capacity < buf->length + n) {
		long new_cap = buf->capacity;
		while(new_cap < buf->length + n) {
			new_cap *= 2;
		}
		double *tmp = (double *)realloc(buf->sound, new_cap * sizeof(double));
		if(tmp == NULL) {
			fprintf(stderr, "could not grow sound buffer to %ld samples\n", new_cap);
			abort();
		}
		buf->sound = tmp;
		buf->capacity = new_cap;
	}
}

void appendtone(outbuf *buf, int freq, double time, double cycles)
{
	int rate = buf->rate;
	int length = buf->length;
	long i, n=time*rate;

	if(freq && cycles)
		n=cycles*rate/freq;

	if(n == 0)
		n=cycles;

	// ensure buffer has space availabe
	outbuf_reserve(buf, n);

	/* 
	   better square code someday, theory here is to use sinewave then square it.
	   to address sin() == 0, i have to keep track of the last value to determine
	   direction

	   this method was written to better address cycles that do not divide the sample rate
	*/
	if(square) {
		double last = -1;

		if(buf->offset)
			last = 1;

		if(freq)
			for(i=0;i<n;i++) {
				double a = (int)(1000*sin(2*M_PI*i*freq/rate + buf->offset*M_PI)) / 1000.0;
				last = buf->sound[length+i] = (a == 0) ? -((last > 0) - (last < 0)) : ((a > 0) - (a < 0));
			}
		else
			for (i = 0; i < n; i++)
				buf->sound[length + i] = 0;
	}
	else
		for(i=0;i<n;i++)
			buf->sound[length+i] = sin(2*M_PI*i*freq/rate + buf->offset*M_PI);

	if(cycles - (int)cycles == 0.5)
		buf->offset = (buf->offset == 0);

	buf->length += n;
}

char *getext(char *filename)
{
	char stack[256], *rval;
	int i, sp = 0;

	for(i=strlen(filename)-1;i>=0;i--) {
		if(filename[i] == '.')
			break;
		stack[sp++] = filename[i];
	}
	stack[sp] = '\0';

	if(sp == strlen(filename) || sp == 0)
		return(NULL);

	if((rval = (char *)malloc(sp * sizeof(char))) == NULL)
		; //do error code

	rval[sp] = '\0';
	for(i=0;i<sp+i;i++)
		rval[i] = stack[--sp];

	return(rval);
}

void usage(void)
{
	fprintf(stderr,"%s",usagetext);
}

// Code below from http://paulbourke.net/dataformats/audio/
/*
   Write an AIFF sound file
   Only do one channel, only support 16 bit.
   Supports sample frequencies of 11, 22, 44KHz (default).
   Little/big endian independent!
*/

// egan: changed code to support any Hz and 8 bit.

void Write_AIFF(FILE * fptr, double *samples, long nsamples, int nfreq, int bits, double amp)
{
	unsigned short v;
	int i;
	unsigned long totalsize;
	double themin, themax, scale, themid;
	unsigned char bit80[10];

	// Write the form chunk
	fprintf(fptr, "FORM");
	totalsize = 4 + 8 + 18 + 8 + (bits / 8) * nsamples + 8;
	fputc((totalsize & 0xff000000) >> 24, fptr);
	fputc((totalsize & 0x00ff0000) >> 16, fptr);
	fputc((totalsize & 0x0000ff00) >> 8, fptr);
	fputc((totalsize & 0x000000ff), fptr);
	fprintf(fptr, "AIFF");

	// Write the common chunk
	fprintf(fptr, "COMM");
	fputc(0, fptr);				// Size
	fputc(0, fptr);
	fputc(0, fptr);
	fputc(18, fptr);
	fputc(0, fptr);				// Channels = 1
	fputc(1, fptr);
	fputc((nsamples & 0xff000000) >> 24, fptr);	// Samples
	fputc((nsamples & 0x00ff0000) >> 16, fptr);
	fputc((nsamples & 0x0000ff00) >> 8, fptr);
	fputc((nsamples & 0x000000ff), fptr);
	fputc(0, fptr);				// Size = 16
	fputc(bits, fptr);

	ConvertToIeeeExtended(nfreq, bit80);
	for (i = 0; i < 10; i++)
		fputc(bit80[i], fptr);

	// Write the sound data chunk
	fprintf(fptr, "SSND");
	fputc((((bits / 8) * nsamples + 8) & 0xff000000) >> 24, fptr);	// Size
	fputc((((bits / 8) * nsamples + 8) & 0x00ff0000) >> 16, fptr);
	fputc((((bits / 8) * nsamples + 8) & 0x0000ff00) >> 8, fptr);
	fputc((((bits / 8) * nsamples + 8) & 0x000000ff), fptr);
	fputc(0, fptr);				// Offset
	fputc(0, fptr);
	fputc(0, fptr);
	fputc(0, fptr);
	fputc(0, fptr);				// Block
	fputc(0, fptr);
	fputc(0, fptr);
	fputc(0, fptr);

	// Find the range
	themin = samples[0];
	themax = themin;
	for (i = 1; i < nsamples; i++) {
		if (samples[i] > themax)
			themax = samples[i];
		if (samples[i] < themin)
			themin = samples[i];
	}
	if (themin >= themax) {
		themin -= 1;
		themax += 1;
	}
	themid = (themin + themax) / 2;
	themin -= themid;
	themax -= themid;
	if (ABS(themin) > ABS(themax))
		themax = ABS(themin);
//  scale = amp * 32760 / (themax);
	scale = amp * ((bits == 16) ? 32760 : 124) / (themax);

	// Write the data
	for (i = 0; i < nsamples; i++) {
		if (bits == 16) {
			v = (unsigned short) (scale * (samples[i] - themid));
			fputc((v & 0xff00) >> 8, fptr);
			fputc((v & 0x00ff), fptr);
		} else {
			v = (unsigned char) (scale * (samples[i] - themid));
			fputc(v, fptr);
		}
	}
}

/*
   Write an WAVE sound file
   Only do one channel, only support 16 bit.
   Supports any (reasonable) sample frequency
   Little/big endian independent!
*/

// egan: changed code to support 8 bit.

void Write_WAVE(FILE * fptr, double *samples, long nsamples, int nfreq, int bits, double amp)
{
	unsigned short v;
	int i;
	unsigned long totalsize, bytespersec;
	double themin, themax, scale, themid;

	// Write the form chunk
	fprintf(fptr, "RIFF");
	totalsize = (bits / 8) * nsamples + 36;
	fputc((totalsize & 0x000000ff), fptr);	// File size
	fputc((totalsize & 0x0000ff00) >> 8, fptr);
	fputc((totalsize & 0x00ff0000) >> 16, fptr);
	fputc((totalsize & 0xff000000) >> 24, fptr);
	fprintf(fptr, "WAVE");
	fprintf(fptr, "fmt ");		// fmt_ chunk
	fputc(16, fptr);			// Chunk size
	fputc(0, fptr);
	fputc(0, fptr);
	fputc(0, fptr);
	fputc(1, fptr);				// Format tag - uncompressed
	fputc(0, fptr);
	fputc(1, fptr);				// Channels
	fputc(0, fptr);
	fputc((nfreq & 0x000000ff), fptr);	// Sample frequency (Hz)
	fputc((nfreq & 0x0000ff00) >> 8, fptr);
	fputc((nfreq & 0x00ff0000) >> 16, fptr);
	fputc((nfreq & 0xff000000) >> 24, fptr);
	bytespersec = (bits / 8) * nfreq;
	fputc((bytespersec & 0x000000ff), fptr);	// Average bytes per second
	fputc((bytespersec & 0x0000ff00) >> 8, fptr);
	fputc((bytespersec & 0x00ff0000) >> 16, fptr);
	fputc((bytespersec & 0xff000000) >> 24, fptr);
	fputc((bits / 8), fptr);		// Block alignment
	fputc(0, fptr);
	fputc(bits, fptr);			// Bits per sample
	fputc(0, fptr);
	fprintf(fptr, "data");
	totalsize = (bits / 8) * nsamples;
	fputc((totalsize & 0x000000ff), fptr);	// Data size
	fputc((totalsize & 0x0000ff00) >> 8, fptr);
	fputc((totalsize & 0x00ff0000) >> 16, fptr);
	fputc((totalsize & 0xff000000) >> 24, fptr);

	// Find the range
	themin = samples[0];
	themax = themin;
	for (i = 1; i < nsamples; i++) {
		if (samples[i] > themax)
			themax = samples[i];
		if (samples[i] < themin)
			themin = samples[i];
	}
	if (themin >= themax) {
		themin -= 1;
		themax += 1;
	}
	themid = (themin + themax) / 2;
	themin -= themid;
	themax -= themid;
	if (ABS(themin) > ABS(themax))
		themax = ABS(themin);
//  scale = amp * 32760 / (themax);
	scale = amp * ((bits == 16) ? 32760 : 124) / (themax);

	// Write the data
	for (i = 0; i < nsamples; i++) {
		if (bits == 16) {
			v = (unsigned short) (scale * (samples[i] - themid));
			fputc((v & 0x00ff), fptr);
			fputc((v & 0xff00) >> 8, fptr);
		} else {
			v = (unsigned char) (scale * (samples[i] - themid));
			fputc(v + 0x80, fptr);
		}
	}
}


/*
 * C O N V E R T   T O   I E E E   E X T E N D E D
 */

/* Copyright (C) 1988-1991 Apple Computer, Inc.
 * All rights reserved.
 *
 * Machine-independent I/O routines for IEEE floating-point numbers.
 *
 * NaN's and infinities are converted to HUGE_VAL or HUGE, which
 * happens to be infinity on IEEE machines.  Unfortunately, it is
 * impossible to preserve NaN's in a machine-independent way.
 * Infinities are, however, preserved on IEEE machines.
 *
 * These routines have been tested on the following machines:
 *    Apple Macintosh, MPW 3.1 C compiler
 *    Apple Macintosh, THINK C compiler
 *    Silicon Graphics IRIS, MIPS compiler
 *    Cray X/MP and Y/MP
 *    Digital Equipment VAX
 *
 *
 * Implemented by Malcolm Slaney and Ken Turkowski.
 *
 * Malcolm Slaney contributions during 1988-1990 include big- and little-
 * endian file I/O, conversion to and from Motorola's extended 80-bit
 * floating-point format, and conversions to and from IEEE single-
 * precision floating-point format.
 *
 * In 1991, Ken Turkowski implemented the conversions to and from
 * IEEE double-precision format, added more precision to the extended
 * conversions, and accommodated conversions involving +/- infinity,
 * NaN's, and denormalized numbers.
 */

#ifndef HUGE_VAL
#define HUGE_VAL HUGE
#endif							/*HUGE_VAL */

#define FloatToUnsigned(f) ((unsigned long)(((long)(f - 2147483648.0)) + 2147483647L) + 1)

void ConvertToIeeeExtended(double num, unsigned char *bytes)
{
	int sign;
	int expon;
	double fMant, fsMant;
	unsigned long hiMant, loMant;

	if (num < 0) {
		sign = 0x8000;
		num *= -1;
	} else {
		sign = 0;
	}

	if (num == 0) {
		expon = 0;
		hiMant = 0;
		loMant = 0;
	} else {
		fMant = frexp(num, &expon);
		if ((expon > 16384) || !(fMant < 1)) {	/* Infinity or NaN */
			expon = sign | 0x7FFF;
			hiMant = 0;
			loMant = 0;			/* infinity */
		} else {				/* Finite */
			expon += 16382;
			if (expon < 0) {	/* denormalized */
				fMant = ldexp(fMant, expon);
				expon = 0;
			}
			expon |= sign;
			fMant = ldexp(fMant, 32);
			fsMant = floor(fMant);
			hiMant = FloatToUnsigned(fsMant);
			fMant = ldexp(fMant - fsMant, 32);
			fsMant = floor(fMant);
			loMant = FloatToUnsigned(fsMant);
		}
	}

	bytes[0] = expon >> 8;
	bytes[1] = expon;
	bytes[2] = hiMant >> 24;
	bytes[3] = hiMant >> 16;
	bytes[4] = hiMant >> 8;
	bytes[5] = hiMant;
	bytes[6] = loMant >> 24;
	bytes[7] = loMant >> 16;
	bytes[8] = loMant >> 8;
	bytes[9] = loMant;
}

uint8_t read6502(uint16_t address)
{
	return ram[address];
}

void write6502(uint16_t address, uint8_t value)
{
	ram[address] = value;
}

void registerevent(event *events, unsigned long int timestamp, const char *format, ...)
{
	va_list args;
	int stored;

	assert(eventnumber < MAXEVENTS);

	va_start(args, /*after*/ format);
	events[eventnumber].timestamp = timestamp;
	stored = vsprintf(events[eventnumber].label,format,args);
	assert(stored < sizeof(events[0].label)/sizeof(char));

	eventnumber++;
}

void printevents(event *events, int rate)
{
	int i;

	fprintf(stderr,"Play List:\n\n");
	for(i=0;i<eventnumber;i++)
		fprintf(stderr,"%06.02f\t%s\n",events[i].timestamp/(float)rate,events[i].label);
	fprintf(stderr,"\n");
}

void gen_write_checked_byte(tapegen *gen, outbuf *buf, unsigned char b)
{	// helper generator method; write and checksum a byte
	gen->write_byte(gen, buf, b);
	gen->checksum_byte(gen, b);
}

void gen_write_block(tapegen *gen, outbuf *buf, unsigned char *data, size_t datalen)
{	// helper generator method; write and checksum a block of bytes
	size_t i;

	for(i=0;i<datalen;i++) {
		gen->write_byte(gen, buf, data[i]);
		gen->checksum_byte(gen, data[i]);
	}
}

void gen_checksum_block(tapegen *gen, unsigned char *data, size_t datalen)
{	// helper generator method; add a block of bytes to checksum
	size_t i;

	for(i=0;i<datalen;i++) {
		gen->checksum_byte(gen, data[i]);
	}
}

void gen_init_checksum(tapegen *gen)
{	// base generator method
	gen->checksum = 0xff;
}

void gen_checksum_byte(tapegen *gen, unsigned char b)
{	// base generator method; add byte to checksum
	gen->checksum ^= b;
}

double fmgen_compute_length(tapegen *gen, unsigned char *data, size_t datalen)
{	// frequency-modulated (FM) base generator method
	unsigned long ones=0, zeros=0;
	size_t i, j;

	for(j=0;j<datalen;j++) {
		unsigned char byte=data[j];
		for(i=0;i<8;i++) {
			if(byte & 0x80)
				ones++;
			else
				zeros++;
			byte <<= 1;
		}
	}
	return ones/(double)gen->freq1 + zeros/(double)gen->freq0;
}

void gen_write_preamble(tapegen *gen, outbuf *buf, double extra)
{	// base generator method
	appendtone(buf, gen->freq_pre, gen->pre_len+extra, 0);
}

void null_write_start(tapegen *gen, outbuf *buf)
{	// no-op (null) generator method
	(void)gen; (void)buf; // suppress warnings
}

void a1tape_write_start(tapegen *gen, outbuf *buf)
{	// Apple 1 generator method; start bit
	appendtone(buf, gen->freq0, 0, 1);
}

void a2tape_write_start(tapegen *gen, outbuf *buf)
{	// Apple 2 generator method; start bit
	// XXX: This reproduces what Apple 2 does but it is not necessary.
	//    A simple bit 0 (2000Hz full cycle) would work.
	appendtone(buf, 2500, 0, 0.5);
	appendtone(buf, gen->freq0, 0, 0.5);
}

void fmgen_write_stop(tapegen *gen, outbuf *buf)
{	// frequency-modulated (FM) base generator method; stop signal
	// NB: 2 cycles to ensure that all three transitions are seen correctly when transmission
	//   hardware has reversed polarity and thus receiver is half cycle behind.
	appendtone(buf, gen->freq_end, 0, 2);
}

void a1tape_write_stop(tapegen *gen, outbuf *buf)
{	// Apple 1/2 generator method; stop bit
	// NB: the stop bit ensures that all three transitions of the last data bit are seen correctly
	appendtone(buf, gen->freq1, 0, 1);
}

void fmgen_write_byte(tapegen *gen, outbuf *buf, unsigned char b)
{	// frequency-modulated (FM) base generator method
	int i;
	for(i=0;i<8;i++) {
		int freq = (b & 0x80) != 0 ? gen->freq1 : gen->freq0;
		appendtone(buf, freq, 0, 1);
		b <<= 1; // MSB to LSB
	}
}

void afm9600_write_byte(tapegen *gen, outbuf *buf, unsigned char b)
{	// frequency-modulated asymmetric 9600 generator method
	int i;
	for(i=0;i<8;i++) {
		if ((b & 0x80) != 0) {  // asymmetric bit 1: 6KHz half-cycle + 12KHz half-cycle
			appendtone(buf, gen->freq0/2, 0, 0.5);
			appendtone(buf, gen->freq0, 0, 0.5);
		}
		else {  // symmetric bit 0
			appendtone(buf, gen->freq0, 0, 1);
		}
		b <<= 1; // MSB to LSB
	}
}

void gen_write_checksum(tapegen *gen, outbuf *buf)
{	// base generator method
	gen->write_byte(gen, buf, gen->checksum);
}

void null_write_checksum(tapegen *gen, outbuf *buf)
{	// no-op (null) generator method
	(void)gen; (void)buf; // suppress warnings
}

void gen_write_filler(tapegen *gen, outbuf *buf, double time)
{	// base generator method
	appendtone(buf, gen->freq_fill, time, 0);
}

void spm15k_write_timed_byte(tapegen *gen, outbuf *buf, unsigned char b, int waitcnt)
{	// phase-modulated synchronous 15k generator method; format one byte
	// Byte signal is similar to RS-232 using format 1-8-N-1
	//   1 start/sync bit [2 samples @ 48Khz] -- receiver syncs to this bit alone
	//          start bit can have either polarity, opposite to the last bit transmitted
	//   8 data bits, big-endian [2.5 samples each @ 48Khz] -- receiver reads bits directly at precise intervals
	//          data bits polarity is wrt/ to start bit: bit 1 has same polarity as start bit, bit 0 is opposite
	//   1 stop/wait bit [varying, min 3 samples] -- wait state giving receiver time to process the byte
	//          stop bit has same polarity as last data bit, with minimal signal level.
	// The actual signal values used were calculated, then hand-tuned experimentally.
	float signal[2 /*sync*/ + (int)(8*2.5f) /*8 bits*/ + 20 /*stop; max wait*/];
	int i, s = 0;
	// NB: polarity flips depending on previous output
	float polarity = -gen->last_polarity;
	float prevbit;

	assert(waitcnt <= 20 /*max wait*/);

	// start bit (sync)
	signal[s++] = 0.82f;
	signal[s++] = 0.82f;

	prevbit = 1.0f; // start with sync bit polarity
	for (i = 0; i < 8; ++i, b <<= 1) {
		float bit = (b & 0x80) > 0 ? 1.0f : -1.0f;
		if (bit == prevbit) {
			// same polarity -- sustainment level
			if ((i & 1) == 0) {
				// even bit; last sample is shared with next bit
				signal[s++] = bit * 0.08f;
				signal[s++] = bit * 0.20f;
				signal[s] = bit * 0.08f;
			} else {
				// odd bit; first sample is shared with previous bit
				signal[s] = (signal[s] + bit * 0.08f) / 2;
				s++;
				signal[s++] = bit * 0.20f;
				signal[s++] = bit * 0.08f;
			}
		} else {
			// opposite polarity -- energetic transition
			if ((i & 1) == 0) {
				// even bit; last sample is shared with next bit
				signal[s++] = bit * 0.85f;
				signal[s++] = bit * 0.78f;
				signal[s] = bit * 0.15f;
			} else {
				// odd bit; first sample is shared with previous bit
				signal[s] = (signal[s] + bit * 0.65f) / 2;
				s++;
				signal[s++] = bit * 0.95f;
				signal[s++] = bit * 0.45f;
			}
		}
		prevbit = bit;
	}

	// stop bit: sustain last level and give receiver time to process the byte
	for (i = 0; i < waitcnt; ++i)
		signal[s++] = prevbit * (0.08f + (i & 1) * 0.12f);
	// patch the last wait sample
	signal[s-1] = prevbit * 0.11f;

	// next polarity reverses depending on the last bit
	gen->last_polarity = prevbit * polarity;
	// convert to correct polarity and append to buffer
	outbuf_reserve(buf, s);
	for (i = 0; i < s; ++i)
		buf->sound[buf->length++] = signal[i] * polarity;
}

void spm15k_write_byte(tapegen *gen, outbuf *buf, unsigned char b)
{	// phase-modulated synchronous 15k generator method; write byte at full speed
	spm15k_write_timed_byte(gen, buf, b, 3 /*full-speed min wait*/);
}

double spm15k_compute_length(tapegen *gen, unsigned char *data, size_t datalen)
{	// phase-modulated synchronous 15k generator method
	(void)data; // suppress warnings; data values are irrelevant
	return datalen * (2 + 8*2.5 + 3) / gen->samp_rate;
}

void spm15k_write_start(tapegen *gen, outbuf *buf)
{	// phase-modulated synchronous 15k generator method; auto-sync bytes
	int i;

	// NB: starting polarity does not matter since the receiver will auto-sync to either.
	//   but the value must be normalized to +/-1.0.
	gen->last_polarity = -1.0;

	// auto-sync sequence; with relaxed waits for sync
	// the bare minimum is 5 sync bytes; plus a few for unexpected conditions
	// NB: values other than 0xFF can be used, but sync waits must then be increased for auto-sync to work.
	//    0xFF is useful for auto-calibrated variants which may start with improperly timed branches.
	for (i = 0; i < 10; ++i)
		spm15k_write_timed_byte(gen, buf, 0xff, 3+2);

	// optional calibration sequence
	if (0 /*disabled*/)
	{
		// calibration start signal; additional wait for calibration processing
		spm15k_write_timed_byte(gen, buf, 0x0F, 3+4);

		// calibration sequence; 10 extras just in case
		for (i = 0; i < 2 * 7 /*calspo*/ * 24 /*calrng*/ + 10 /*extras*/; ++i)
			spm15k_write_timed_byte(gen, buf, 0x55 /*calval*/, 3+4);

		// data auto-sync sequence; also provides extra time for calibration calcs.
		// NB: final calibration calculations take ~3 bytes time; increase the sync count
		//   to compensate for any additional processing if necessary.
		for (i = 0; i < 8; ++i)
			spm15k_write_timed_byte(gen, buf, 0xff, 3);
	}

	// data start signal; relaxed wait for data start
	spm15k_write_timed_byte(gen, buf, 0x00, 3+2);
}

void spm15k_write_stop(tapegen *gen, outbuf *buf)
{	// phase-modulated synchronous 15k generator method; stop signal
	const int waitcnt = 12; // end transmission delay
	float signal[12 + 5];
	int i, s = 0;
	float polarity = gen->last_polarity;
	// sustain last level and delay the next transition
	for (i = 0; i < waitcnt; ++i)
	    signal[s++] = 1.0f * (0.08f + (i & 1) * 0.12f);

	// two energetic transitions to stop the receiver
	signal[s++] = -1.0f;
	signal[s++] = -0.60f;
	signal[s++] = 0.0f;
	signal[s++] = 1.0f;
	signal[s++] = 0.60f;

	// convert to correct polarity and append to buffer
	outbuf_reserve(buf, s);
	for (i = 0; i < s; ++i)
		buf->sound[buf->length++] = signal[i] * polarity;
}
