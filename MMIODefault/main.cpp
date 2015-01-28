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
#include <intrin.h>
#include <Windows.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "ETWClient/ETWClient.h"

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Helper macro to prevent warnings about unreferenced arguments.
#define UNUSED_ARG(x)                \
    do {                             \
        (x);                         \
    __pragma(warning(push));         \
    __pragma(warning(disable:4127)); \
    } while(0);                      \
    __pragma(warning(pop))

/// @summary Define the size of the default file mapping, in bytes.
#define MAPPING_SIZE    (2ULL * 1024ULL * 1024ULL)

/// @summary Use the _rotl intrinsic, which is significantly more efficient
/// on MSVC; use (x << y) | (x >> (32 - y)) on gcc for the same effect.
#define ROTL32(x, y)    _rotl((x), (y))

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
    uint32_t  Hash[4];   /// The 128-bit hash value of the input data.
};

/// @summary Helper class to initialize ETW when the application is loaded and
/// shut it down when the application exits.
class EventTrace
{
public:
    inline  EventTrace(void) { ETWInitialize(); }
    inline ~EventTrace(void) { ETWShutdown();   }
private:
    EventTrace(EventTrace &other);              /* non-copyable */
    EventTrace& operator =(EventTrace &other);  /* non-copyable */
};

/*///////////////
//   Globals   //
///////////////*/
/// @summary The clock frequency as returned by QueryPerformanceFrequency.
/// This value is used to convert timestamps into seconds.
static LARGE_INTEGER QPC_FREQUENCY = { 0 };

/// @summary Perform initialization of our event tracing system at some point
/// before control is transferred to main(), and shut down event tracing at
/// some point after main returns.
/// TODO: Will the compiler try and be smart and optimize this out?
EventTrace EVENT_TRACE;

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
/// @return true if the file information was obtained.
static bool print_file_info(char const *path, int64_t &file_size)
{
    struct _stat64 st;
    if (_stati64(path, &st) == 0)
    {   // the file exists, so print out some basic information.
        fprintf(stdout, "STATUS: Found file \'%s\', %" PRIu64 " bytes.\n", path, st.st_size);
        file_size = st.st_size;
        return true;
    }
    else if (errno == ENOENT)
    {   // the file does not exist.
        fprintf(stderr, "ERROR: The file \'%s\' cannot be found.\n", path);
        return false;
    }
    else
    {   // some other error occurred.
        fprintf(stderr, "ERROR: Unable to stat file \'%s\': %s\n", path, strerror(errno));
        return false;
    }
}

/// @summary Calculate the offset and size of the next range to map.
/// @param state The file state associated with the file being mapped.
/// @param offset_high On return, this value is set to the high 32-bits of the mapping offset.
/// @param offset_low On return, this value is set to the low 32-bits of the mapping offset.
/// @param bytes_to_map On return, this value is set to the size to pass as dwNumberOfBytesToMap when calling MapViewOfFile.
/// @param actual_size On return, this value is set to the actual size of the file mapping, in bytes.
/// @return false if end-of-file, or true to proceed with mapping.
static bool next_mapping(file_state_t *state, DWORD &offset_high, DWORD &offset_low, SIZE_T &bytes_to_map, size_t &actual_size)
{
    int64_t next_offset = state->FileOffset;
    int64_t file_size   = state->FileSize;
    if (file_size - next_offset <= 0)
    {   // next range starts outside of the valid range. we're done.
        offset_high  = 0;
        offset_low   = 0;
        bytes_to_map = 0;
        actual_size  = 0;
        return false;
    }
    if (file_size - next_offset > MAPPING_SIZE)
    {   // the amount of data remaining is more than MAPPING_SIZE bytes, so
        // map a MAPPING_SIZE portion of the file for read access.
        bytes_to_map = MAPPING_SIZE;
        actual_size  = MAPPING_SIZE;
    }
    else
    {   // we're at the end of the file, so only a portion will be mapped.
        // in this case, specify zero for the size in MapViewOfFile[Ex].
        bytes_to_map = 0;
        actual_size  = size_t(file_size - next_offset);
    }
    // break the 64-bit offset into high and low 32-bit offsets.
    // the offset must be a multiple of the system allocation granularity (64KB).
    offset_high = (DWORD) ((uint64_t(next_offset) & 0xFFFFFFFF00000000ULL) >> 32);
    offset_low  = (DWORD) ((uint64_t(next_offset) & 0x00000000FFFFFFFFULL));
    return true;
}

/// @summary Open a file, create the file mapping, and map the initial view.
/// @param state The file state to initialize. All fields will be overwritten.
/// @param path The NULL-terminated ANSI string specifying the path of the file.
/// @param file_size The size of the file, in bytes.
/// @return true if the file was opened successfully.
static bool open_file(file_state_t *state, char const *path, int64_t file_size)
{
    DWORD  access      = GENERIC_READ;
    DWORD  share_mode  = FILE_SHARE_READ;
    DWORD  create_mode = OPEN_EXISTING;
    DWORD  flags       = FILE_FLAG_SEQUENTIAL_SCAN;
    HANDLE fd          = INVALID_HANDLE_VALUE;
    HANDLE md          = INVALID_HANDLE_VALUE;
    DWORD  oh          = 0; // offset high 32-bits
    DWORD  ol          = 0; // offset low 32-bits
    SIZE_T ms          = 0; // number of bytes to map
    size_t as          = 0; // actual number of bytes mapped
    void  *view        = NULL;

    state->StartTime   = timestamp();
    state->FileSize    = file_size;
    state->FileOffset  = 0;
    state->Fildes      = INVALID_HANDLE_VALUE;
    state->Filmap      = NULL;
    state->MapSize     = 0;
    state->MapBase     = NULL;
    state->BufferBeg   = NULL;
    state->BufferEnd   = NULL;
    state->BufferCur   = NULL;
    state->Hash[0]     = 0;
    state->Hash[1]     = 0;
    state->Hash[2]     = 0;
    state->Hash[3]     = 0;

    if ((fd = CreateFileA(path, access, share_mode, NULL, create_mode, flags, NULL)) == INVALID_HANDLE_VALUE)
    {   // unable to open the file; fail immediately.
        fprintf(stderr, "ERROR: Unable to open file \'%s\': 0x%08X\n", path, GetLastError());
        goto error_cleanup;
    }
    if ((md = CreateFileMappingA(fd, NULL, PAGE_READONLY, 0, 0, NULL)) == NULL)
    {   // unable to create the file mapping; fail immediately.
        fprintf(stderr, "ERROR: Unable to create file mapping: \'%s\': 0x%08X\n", path, GetLastError());
        goto error_cleanup;
    }
    if (!next_mapping(state, oh, ol, ms, as))
    {   // this would only happen if the file is zero bytes.
        fprintf(stderr, "ERROR: Cannot process a zero-byte file \'%s\' (%" PRId64 " bytes).\n", path, file_size);
        goto error_cleanup;
    }
    if ((view = MapViewOfFileEx(md, FILE_MAP_READ, oh, ol, ms, NULL)) == NULL)
    {   // unable to create the initial mapped view.
        fprintf(stderr, "ERROR: Unable to map view of file \'%s\': 0x%08X\n", path, GetLastError());
        goto error_cleanup;
    }

    state->Fildes    = fd;
    state->Filmap    = md;
    state->MapSize   = as; // store the actual size
    state->MapBase   = view;
    state->BufferBeg = (uint8_t*) view;
    state->BufferEnd = (uint8_t*) view + as;
    state->BufferCur = (uint8_t*) view;
    return true;

error_cleanup:
    if (md != NULL) CloseHandle(md);
    if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd);
    return false;
}

/// @summary Update the mapped range of the file to the next contiguous block of data.
/// @param state The state associated with the file.
/// @param eof On return, set to true if end-of-file was reached.
/// @return false if end-of-file was reached or an error occurred.
static bool update_view(file_state_t *state, bool &eof)
{
    DWORD  offset_high   = 0;
    DWORD  offset_low    = 0;
    SIZE_T bytes_to_map  = 0;
    size_t actual_size   = 0;
    void  *view          = NULL;

    // update the current file offset:
    state->FileOffset   += state->MapSize;

    // unmap any existing view, and invalidate pointers.
    if (state->MapBase  != NULL)
    {   // don't reset state->MapSize, as next_mapping needs it.
        UnmapViewOfFile(state->MapBase);
        state->MapBase   = NULL;
        state->BufferBeg = NULL;
        state->BufferEnd = NULL;
        state->BufferCur = NULL;
    }
    if (!next_mapping(state, offset_high, offset_low, bytes_to_map, actual_size))
    {   // end-of-file was reached; we're done.
        eof  = true;
        return false;
    }
    if ((view = MapViewOfFileEx(state->Filmap, FILE_MAP_READ, offset_high, offset_low, bytes_to_map, NULL)) == NULL)
    {   // unable to map the next view.
        fprintf(stderr, "ERROR: Unable to map view at byte offset %" PRId64 ": 0x%08X.\n", state->FileOffset, GetLastError());
        eof  = false;
        return false;
    }

    eof  = false;
    state->MapBase    = NULL;
    state->MapSize    = actual_size;
    state->BufferBeg  = (uint8_t*) view;
    state->BufferEnd  = (uint8_t*) view + actual_size;
    state->BufferCur  = (uint8_t*) view;
    return true;
}

/// @summary Unmap any active views and close any open handles.
/// @param state The state associated with the file to close.
static void close_file(file_state_t *state)
{
    if (state->MapBase != NULL) UnmapViewOfFile(state->MapBase);
    if (state->Filmap  != NULL) CloseHandle(state->Filmap);
    if (state->Fildes  != INVALID_HANDLE_VALUE) CloseHandle(state->Fildes);
    state->FileOffset   = 0;
    state->Fildes       = INVALID_HANDLE_VALUE;
    state->Filmap       = NULL;
    state->MapSize      = 0;
    state->MapBase      = NULL;
    state->BufferBeg    = NULL;
    state->BufferEnd    = NULL;
    state->BufferCur    = NULL;
}

static bool elevate_process_privileges(void)
{
    TOKEN_PRIVILEGES tp;
    HANDLE        token;
    LUID          luid1;
    LUID          luid2;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token) && 
        LookupPrivilegeValue(NULL, SE_MANAGE_VOLUME_NAME, &luid1) && 
        LookupPrivilegeValue(NULL, SE_CREATE_GLOBAL_NAME, &luid2))
    {   // error handling? what error handling?
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        BOOL r1 = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL);
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid2;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        BOOL r2 = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL);
        CloseHandle(token);
        return (r1 && r2);
    }
    else return false;
}

static inline uint32_t get_block32(uint32_t const *buffer, int i)
{
    return buffer[i];
}

static inline uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

static void hash_init(uint32_t seed, void *state128)
{
    ((uint32_t*)state128)[0] = seed;
    ((uint32_t*)state128)[1] = seed;
    ((uint32_t*)state128)[2] = seed;
    ((uint32_t*)state128)[3] = seed;
}

static void hash_update(void const *key, size_t length, void *state128)
{
    uint8_t  const *data    = (uint8_t  const*) key;
    int      const  nblocks =  int(length) / 16;
    uint32_t const  c1      = 0x239b961b;
    uint32_t const  c2      = 0xab0e9789;
    uint32_t const  c3      = 0x38b34ae5;
    uint32_t const  c4      = 0xa1e38b93;
    uint32_t const *blocks  = (uint32_t const*)(data + nblocks * 16);
    uint8_t  const *tail    = (uint8_t  const*)(data + nblocks * 16);
    uint32_t        h1      =((uint32_t*) state128)[0];
    uint32_t        h2      =((uint32_t*) state128)[1];
    uint32_t        h3      =((uint32_t*) state128)[2];
    uint32_t        h4      =((uint32_t*) state128)[3];

    // process the majority of the data 128 bytes at a time.
    for (int i  = -nblocks; i; i++)
    {
        uint32_t k1 = get_block32(blocks, i * 4 + 0);
        uint32_t k2 = get_block32(blocks, i * 4 + 1);
        uint32_t k3 = get_block32(blocks, i * 4 + 2);
        uint32_t k4 = get_block32(blocks, i * 4 + 3);

        k1 *= c1;             k1  = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
        h1  = ROTL32(h1, 19); h1 += h2;             h1  = h1 * 5 + 0x561ccd1b;
        k2 *= c2;             k2  = ROTL32(k2, 16); k2 *= c3; h2 ^= k2;
        h2  = ROTL32(h2, 17); h2 += h3;             h2  = h2 * 5 + 0x0bcaa747;
        k3 *= c3;             k3  = ROTL32(k3, 17); k3 *= c4; h3 ^= k3;
        h3  = ROTL32(h3, 15); h3 += h4;             h3  = h3 * 5 + 0x96cd1c35;
        k4 *= c4;             k4  = ROTL32(k4, 18); k4 *= c1; h4 ^= k4;
        h4  = ROTL32(h4, 13); h4 += h1;             h4  = h4 * 5 + 0x32ac3b17;
    }

    // process the remaining 0-15 bytes of data. the switch statement falls through.
    uint32_t k1 = 0;
    uint32_t k2 = 0;
    uint32_t k3 = 0;
    uint32_t k4 = 0;
    switch (length & 15)
    {
        case 15: k4 ^= tail[14] << 16;
        case 14: k4 ^= tail[13] <<  8;
        case 13: k4 ^= tail[12] <<  0;
                 k4 *= c4; k4 = ROTL32(k4, 18); k4 *= c1; h4 ^= k4;

        case 12: k3 ^= tail[11] << 24;
        case 11: k3 ^= tail[10] << 16;
        case 10: k3 ^= tail[ 9] <<  8;
        case  9: k3 ^= tail[ 8] <<  0;
                 k3 *= c3; k3 = ROTL32(k3, 17); k3 *= c4; h3 ^= k3;

        case  8: k2 ^= tail[ 7] << 24;
        case  7: k2 ^= tail[ 6] << 16;
        case  6: k2 ^= tail[ 5] <<  8;
        case  5: k2 ^= tail[ 4] <<  0;
                 k2 *= c2; k2 = ROTL32(k2, 16); k2 *= c3; h2 ^= k2;

        case  4: k1 ^= tail[ 3] << 24;
        case  3: k1 ^= tail[ 2] << 16;
        case  2: k1 ^= tail[ 1] <<  8;
        case  1: k1 ^= tail[ 0] <<  0;
                 k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
    }

    // store the updated state:
    ((uint32_t*) state128)[0] = h1;
    ((uint32_t*) state128)[1] = h2;
    ((uint32_t*) state128)[2] = h3;
    ((uint32_t*) state128)[3] = h4;
}

static void hash_finish(int64_t file_size, void *state128)
{   // looks like this only works for files <= 4GB.
    uint32_t fs =  (uint32_t )  file_size;
    uint32_t h1 = ((uint32_t*)  state128)[0];
    uint32_t h2 = ((uint32_t*)  state128)[1];
    uint32_t h3 = ((uint32_t*)  state128)[2];
    uint32_t h4 = ((uint32_t*)  state128)[3];

    h1 ^= fs; h2 ^= fs; h3 ^= fs; h4 ^= fs;
    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    h1  = fmix32(h1);
    h2  = fmix32(h2);
    h3  = fmix32(h3);
    h4  = fmix32(h4);

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    ((uint32_t*) state128)[0] = h1;
    ((uint32_t*) state128)[1] = h2;
    ((uint32_t*) state128)[2] = h3;
    ((uint32_t*) state128)[3] = h4;
}

static void print_hash(FILE *fp, void const *hash)
{
    uint8_t const *u8 = (uint8_t const*) hash;
    fprintf(fp, "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
            u8[ 0], u8[ 1], u8[ 2], u8[ 3],
            u8[ 4], u8[ 5], u8[ 6], u8[ 7],
            u8[ 8], u8[ 9], u8[10], u8[11],
            u8[12], u8[13], u8[14], u8[15]);
}

/*///////////////////////
//  Public Functions   //
///////////////////////*/
int main(int argc, char **argv)
{
    int64_t      done_time = 0;
    int64_t      file_size = 0;
    double       elapsed_s = 0.0;
    file_state_t file_state;

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
    if (!elevate_process_privileges())
    {   // unable to acquire the necessary privileges.
        fprintf(stderr, "ERROR: Unable to acquire privileges.\n");
        exit(EXIT_FAILURE);
    }
    if (!print_file_info(argv[1], file_size))
    {   // unable to stat the input file.
        exit(EXIT_FAILURE);
    }
    if (!open_file(&file_state, argv[1], file_size))
    {   // unable to open the input file or create the file mapping.
        exit(EXIT_FAILURE);
    }

    hash_init(0, file_state.Hash);
    bool eof = false;
    do
    {   // emit a marker event for viewing in WPA.
        ETWMarkerMain("Tick");
        // perform some computation on each byte in the mapped range.
        for (size_t i = 0; i < 100; ++i)
        {
            hash_update(file_state.BufferBeg, file_state.MapSize, file_state.Hash);
        }
        // update the view to point to the next contiguous range in the file.
        // eof will be set to true if we've hit end-of-file.
        update_view(&file_state, eof);
    } while (!eof);
    hash_finish(file_state.FileSize, file_state.Hash);
    print_hash (stdout, file_state.Hash);
    close_file (&file_state);

    done_time = timestamp();
    elapsed_s = seconds(done_time - file_state.StartTime);
    fprintf(stdout, "STATUS: Finished run in %f seconds (%0.3f bytes/sec).\n", elapsed_s, file_size / elapsed_s);
    exit(EXIT_SUCCESS);
}
