/*****************************************************************
|
|      File: PcmDiff.cpp
|
|      BlueTune - PCM Diff Tool
|
|      (c) 2002-2003 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "Neptune.h"

/*----------------------------------------------------------------------
|    globals
+---------------------------------------------------------------------*/
struct {
    const char*  filename_1;
    const char*  filename_2;
    unsigned int frame_size;
    unsigned int channel_count;
    unsigned int threshold;
} Options;

/*----------------------------------------------------------------------
|    PrintUsageAndExit
+---------------------------------------------------------------------*/
static void
PrintUsageAndExit()
{
    printf("pcmdiff [options] <filename1> <filename2>\n"
           "  options:\n"
           "  --frame-size=<n>: audio frame size (in samples) [def=1152]\n"
           "  --channels=<n>:   number of channels [def=2]\n"
           "  --threshold=<n>:  threshold for detecting a difference [def=1]\n"
        );
    exit(1);
}

/*----------------------------------------------------------------------
|    ParseCommandLine
+---------------------------------------------------------------------*/
static void
ParseCommandLine(char** args)
{
    // default options
    Options.filename_1    = NULL;
    Options.filename_2    = NULL;
    Options.frame_size    = 1152;
    Options.channel_count = 2;
    Options.threshold     = 1;
    char* arg;

    while((arg = *args++)) {
        if (!strncmp(arg, "--frame-size=", 13)) {
            Options.frame_size = strtoul(arg+13, NULL, 10);
        } else if (!strncmp(arg, "--channels=", 11)) {
            Options.channel_count = strtoul(arg+11, NULL, 10);
        } else if (!strncmp(arg, "--threshold=", 12)) {
            Options.threshold = strtoul(arg+12, NULL, 10);
        } else if (Options.filename_1 == NULL) {
            Options.filename_1 = arg;
        } else if (Options.filename_2 == NULL) {
            Options.filename_2 = arg;
        } else {
            fprintf(stderr, "invalid argument '%s'\n", arg);
        }
    }

    // check args
    if (Options.filename_1 == NULL || Options.filename_2 == NULL) {
        fprintf(stderr, "missing filename\n");
        PrintUsageAndExit();
    }
}

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
    NPT_Result result;

    // parse command line
    ParseCommandLine(argv+1);

    // open first file
    NPT_FileByteStream* in1 = 
        new NPT_FileByteStream(Options.filename_1,
                               NPT_FILE_BYTE_STREAM_MODE_READ);
    result = in1->GetOpenResult();
    if (NPT_FAILED(result)) {
        fprintf(stderr, "cannot open file %s [error %d]\n",
                Options.filename_1, result);
    }

    // open first file
    NPT_FileByteStream* in2 = 
        new NPT_FileByteStream(Options.filename_2,
                               NPT_FILE_BYTE_STREAM_MODE_READ);
    result = in2->GetOpenResult();
    if (NPT_FAILED(result)) {
        fprintf(stderr, "cannot open file %s [error %d]\n",
                Options.filename_2, result);
    }

    NPT_Size chunk_size = Options.frame_size*Options.channel_count;
    short* buffer1 = new short[chunk_size];
    short* buffer2 = new short[chunk_size];
    for (unsigned long frame=0; ; frame++) {
        NPT_Size read_count;
        result = in1->Read(buffer1, chunk_size*2, &read_count);
        if (NPT_FAILED(result) || read_count != chunk_size*2) {
            break;
        }
        result = in2->Read(buffer2, chunk_size*2, &read_count);
        if (NPT_FAILED(result) || read_count != chunk_size*2) {
            break;
        }

        for (unsigned int s=0; s<Options.frame_size; s++) {
            short s1 = buffer1[s];
            short s2 = buffer2[s];
            unsigned int diff = s2-s1 >= 0 ? s2-s1 : s1-s2;
            if (diff > Options.threshold) {
                printf("%05d@%08ld: %d/%d [%d]\n", s, frame, s1, s2, diff);
            }
        }
    };
    
    return 0;
}
