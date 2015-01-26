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

#define MAPPING_SIZE    (1LL * 1024LL * 1024LL)

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary 
struct file_state_t
{
    int64_t   StartTime; /// The timestamp at which the operation started.
    int64_t   FileSize;  /// The size of the file, in bytes.
    int64_t   FileOffset;/// The current offset within the file, in bytes.
    HANDLE    Fildes;    /// The file handle, as returned by CreateFile.
    HANDLE    Filmap;    /// The mapping handle, as returned by CreateFileMapping.
    size_t    MapSize;   /// The size of the mapped range, in bytes.
    void     *MapBase;   /// The pointer to the start of the mapped range.
    uint8_t  *BufferBeg; /// The first byte of the user's view of the mapped range.
    uint8_t  *BufferEnd; /// The end of the user's view of the mapped range.
    uint8_t  *BufferCur; /// The current read cursor within the mapped range.
    uint64_t  Checksum;  /// The checksum value.
};

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
    fprintf(stdout, "mmiodefault.exe: Read a memory-mapped file without prefetching.\n");
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
/// @param file_size On return, this location is updated with the size of the file, in bytes.
static void print_file_info(char const *path, int64_t &file_size)
{
    struct _stat64 st;
    if (_stati64(path, &st) == 0)
    {   // the file exists, so print out some basic information.
        fprintf(stdout, "STATUS: Found file \'%s\', %" PRIu64 " bytes.\n", path, st.st_size);
        file_size = st.st_size;
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

static bool next_mapping(file_state_t *state, DWORD &offset_high, DWORD &offset_low, size_t &mapping_size)
{
    int64_t next_offset = state->FileOffset + state->MapSize;
    int64_t file_size   = state->FileSize;
    size_t  map_size    = MAPPING_SIZE;

    if (file_size - next_offset < 0)
    {   // next range starts outside of the valid range. we're done.
        offset_high  = 0;
        offset_low   = 0;
        mapping_size = 0;
        return false;
    }
    if (file_size - next_offset > MAPPING_SIZE)
    {   // the amount of data remaining is more than MAPPING_SIZE bytes, so
        // map a MAPPING_SIZE portion of the file for read access.
        mapping_size = MAPPING_SIZE;
    }
    else
    {   // we're at the end of the file, so only a portion will be mapped.
        // in this case, specify zero for the size in MapViewOfFile[Ex].
        mapping_size = file_size - next_offset;
    }
    // break the 64-bit offset into high and low 32-bit offsets.
    // the offset must be a multiple of the system allocation granularity (64KB).
    offset_high = (next_offset & 0xFFFFFFFF00000000LL) >> 32;
    offset_low  = (next_offset & 0x00000000FFFFFFFFLL);
}

static bool open_file(file_state_t *state, char const *path, int64_t file_size)
{
    DWORD  access      = GENERIC_READ;
    DWORD  share_mode  = FILE_SHARE_READ;
    DWORD  create_mode = OPEN_EXISTING;
    DWORD  flags       = FILE_FLAG_SEQUENTIAL_SCAN;
    HANDLE fd          = INVALID_HANDLE_VALUE;
    HANDLE md          = INVALID_HANDLE_VALUE;

    if ((fd = CreateFileA(path, access, share_mode, NULL, create_mode, flags, NULL)) == INVALID_HANDLE_VALUE)
    {
        //
    }
    if ((md = CreateFileMappingA(fd, NULL, FILE_MAP_READ, 0, 0, NULL)) == NULL)
    {
        // 
    }
}
struct file_state_t
{
    int64_t   StartTime; /// The timestamp at which the operation started.
    int64_t   FileSize;  /// The size of the file, in bytes.
    int64_t   FileOffset;/// The current offset within the file, in bytes.
    HANDLE    Fildes;    /// The file handle, as returned by CreateFile.
    HANDLE    Filmap;    /// The mapping handle, as returned by CreateFileMapping.
    size_t    MapSize;   /// The size of the mapped range, in bytes.
    void     *MapBase;   /// The pointer to the start of the mapped range.
    uint8_t  *BufferBeg; /// The first byte of the user's view of the mapped range.
    uint8_t  *BufferEnd; /// The end of the user's view of the mapped range.
    uint8_t  *BufferCur; /// The current read cursor within the mapped range.
    uint64_t  Checksum;  /// The checksum value.
};

static void unmap_view(file_state_t *state)
{

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

    int64_t file_size  = 0;
    print_file_info(argv[1], file_size);
    ETWInitialize();
    {
        file_state_t    state;
        state.StartTime = timestamp();
        state.FileSize  = file_size;
        state.FileOffset= 0;
        state.Fildes    = CreateFileA(argv[0], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        state.Filmap    = CreateFileMapping(state.Fildes, NULL, PAGE_READONLY, 0, 0, NULL);
        state.BasePtr   = MapViewOfFileEx(state.Filmap, FILE_MAP_READ, 0, 0, MAPPING_SIZE, NULL);
        state.MapSize   = MAPPING_SIZE;
        state.Checksum  = 0;
    }
    ETWShutdown();
    exit(EXIT_SUCCESS);
}
