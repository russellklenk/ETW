/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines the application entry point. 
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <Windows.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "ETWClient/ETWClient.h"

/*/////////////////
//   Constants   //
/////////////////*/
#define UNUSED_ARG(x)                \
    do {                             \
        (x);                         \
    __pragma(warning(push));         \
    __pragma(warning(disable:4127)); \
    } while(0);                      \
    __pragma(warning(pop))

/*//////////////////
//   Data Types   //
//////////////////*/

/*///////////////
//   Globals   //
///////////////*/
/// @summary The clock frequency as returned by QueryPerformanceFrequency.
/// This value is used to convert timestamps into seconds.
static LARGE_INTEGER QPC_FREQUENCY = { 0 };

/*///////////////////////
//   Local Functions   //
///////////////////////*/
/// @summary Print usage information, and then exit.
static void print_usage(void)
{
    fprintf(stdout, "mmiodefault.exe: Read a memory-mapped file with prefetching.\n");
    fprintf(stdout, "USAGE: mmiodefault.exe INFILE\n");
    fprintf(stdout, "  INFILE: The path of the input file to process.\n");
    fprintf(stdout, "\n");
    exit(EXIT_FAILURE);
}

/// @summary Get the frequency of the system high-resolution timer.
/// @return true if the timer is available.
static bool timer_init(void)
{
    return (QueryPerformanceFrequency(&QPC_FREQUENCY) == TRUE);
}

/// @summary Read the high-resolution timer to get a timestamp value.
/// @return The timestamp value, in unspecified ticks.
static inline int64_t timestamp(void)
{
    LARGE_INTEGER tsc = {0};
    QueryPerformanceCounter(&tsc);
    return tsc.QuadPart;
}

/// @summary Convert a timestamp value or delta to seconds.
/// @param ts The timestamp or time delta, in ticks.
/// @return The time value in seconds.
static inline double seconds(int64_t ts)
{
    double frequency = (double) QPC_FREQUENCY.QuadPart;
    return (double) ts / frequency;
}

/// @summary Stat the input file and print out basic file attributes. This is 
/// done to ensure that the file exists prior to continuing.
/// @param path The path of the input file.
static void print_file_info(char const *path)
{
    struct _stat64 st;
    if (_stati64(path, &st) == 0)
    {   // the file exists, so print out some basic information.
        fprintf(stdout, "STATUS: Found file \'%s\', %" PRIu64 " bytes.\n", path, st.st_size);
    }
    else if (errno == ENOENT)
    {   // the file does not exist.
        fprintf(stderr, "ERROR: The file \'%s\' cannot be found.\n", path);
        exit(EXIT_FAILURE);
    }
    else
    {   // some other error occurred.
        fprintf(stderr, "ERROR: Unable to stat file \'%s\': %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/*///////////////////////
//  Public Functions   //
///////////////////////*/
int main(int argc, char **argv)
{
    if (argc < 2)
    {   // one or more required arguments are missing.
        fprintf(stderr, "ERROR: Missing argument INFILE.\n\n");
        print_usage();
    }
    if (!timer_init())
    {   // the high-resolution timer is not available.
        fprintf(stderr, "ERROR: High-resolution timer not available.\n\n");
        print_usage();
    }

    print_file_info(argv[1]);
    ETWInitialize();
    {
        // 
    }
    ETWShutdown();
    exit(EXIT_SUCCESS);
}

