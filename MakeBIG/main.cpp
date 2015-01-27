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
#include <assert.h>
#include <Windows.h>
#include <sys/stat.h>

/*/////////////////
//   Constants   //
/////////////////*/

/*//////////////////
//   Data Types   //
//////////////////*/

/*///////////////
//   Globals   //
///////////////*/

/*///////////////////////
//   Local Functions   //
///////////////////////*/
/// @summary Print usage information, and then exit.
static void print_usage(void)
{
    fprintf(stdout, "makebig.exe: Generate 16 1GB files.\n");
    fprintf(stdout, "USAGE: makebig.exe OUTPATH\n");
    fprintf(stdout, "  OUTPATH: The directory path where the output files are written.\n");
    fprintf(stdout, "\n");
    exit(EXIT_FAILURE);
}

/// @summary Request the ability to pre-size a file without filling the contents 
/// with zero bytes (which is a security concern.) This application will overwrite
/// the file contents completely anyway.
/// @return true if the requested privileges were granted.
static bool elevate_process_privileges(void)
{
    TOKEN_PRIVILEGES tp;
    HANDLE        token;
    LUID           luid;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token) && 
        LookupPrivilegeValue(NULL, SE_MANAGE_VOLUME_NAME, &luid))
    {   // error handling? what error handling?
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        BOOL r1 = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL);
        CloseHandle(token);
        return (r1 == TRUE);
    }
    else return false;
}

/// @summary Convert the output path from UTF-8 to UCS-2 and ensure that it has a 
/// trailing directory separator appended for easy appending of filenames.
/// @param path A NULL-terminated UTF-8 string representing the path to convert.
/// @param last_separator On return, this location points to the last directory separator character, or NULL if path is an empty string.
/// @return The UCS-2 path string, or NULL. Free the returned buffer using a standard C library free() call.
static WCHAR* make_output_path(char const *path, WCHAR *&last_separator)
{
    WCHAR *buffer = NULL;
    size_t dirlen = strlen(path);
    int    seplen = 0;
    int    nchars = 0;
    int    nbytes =-1;

    struct _stat64 st;
    if (_stati64(path, &st) != 0 || (st.st_mode & _S_IFDIR) == 0)
    {
        if ((st.st_mode & _S_IFDIR) == 0)
        {   // the specified path is not a directory.
            fprintf(stderr, "ERROR: The specified path \'%s\' is not a directory.\n", path);
            return false;
        }
        else if (errno == ENOENT)
        {   // the file does not exist.
            fprintf(stderr, "ERROR: The directory \'%s\' cannot be found.\n", path);
            return false;
        }
        else
        {   // some other error occurred.
            fprintf(stderr, "ERROR: Unable to stat path \'%s\': %s\n", path, strerror(errno));
            return false;
        }
    }

    if (dirlen > 0)
    {   // check to see if we need to append a path separator.
        char last  = path[dirlen - 1];
        if  (last != '\\' && last != '/')
        {   // need to include one extra separator.
            seplen = 1;
        }
    }
    // determine the number of UCS-2 characters required.
    nbytes = dirlen > 0 ? int(dirlen) : -1;
    nchars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, nbytes, NULL, 0);
    if (dirlen > 0 && nchars == 0)
    {   // the path cannot be converted from UTF-8 to UCS-2.
        last_separator = NULL;
        return NULL;
    }
    // allocate storage for the UCS-2 path string.
    nchars = nchars + seplen + 1;
    buffer = (WCHAR*) malloc(nchars * sizeof(WCHAR));
    if (buffer == NULL)
    {   // unable to allocate the required storage.
        last_separator = NULL;
        return NULL;
    }
    memset(buffer, 0, nchars * sizeof(WCHAR));
    // perform the actual conversion to UCS-2 and store the result.
    if (dirlen > 0)
    {
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, int(dirlen), buffer, nchars) == 0)
        {   // unable to perform the conversion. since we did this once 
            // previously, this shouldn't ever really happen.
            free(buffer);
            return NULL;
        }
        last_separator = buffer + (nchars - 1);
        wcscat(buffer, L"\\");
    }
    return buffer;
}

static inline int64_t align_up(int64_t size, size_t pow2)
{
    assert((pow2 & (pow2-1)) == 0);
    return (size == 0) ? int64_t(pow2) : ((size + int64_t(pow2-1)) & ~int64_t(pow2-1));
}

static size_t physical_sector_size(HANDLE fd)
{
    size_t const DefaultPhysicalSectorSize = 4096;
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR desc;
    STORAGE_PROPERTY_QUERY    query;
    memset(&desc  , 0, sizeof(desc));
    memset(&query , 0, sizeof(query));
    query.QueryType  = PropertyStandardQuery;
    query.PropertyId = StorageAccessAlignmentProperty;
    DWORD bytes = 0;
    BOOL result = DeviceIoControl(fd, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc, sizeof(desc), &bytes, NULL);
    if (!result)  return DefaultPhysicalSectorSize;
    else return desc.BytesPerPhysicalSector;
}

static bool write_output_file(WCHAR const *outpath, WCHAR const *filename, int64_t size)
{
    FILE_END_OF_FILE_INFO eof;
    FILE_ALLOCATION_INFO  sec;
    SYSTEM_INFO           sysinfo;
    int64_t               file_size = 0;
    size_t                sector_sz = 0;
    size_t                num_pages = 0;
    size_t                path_len  = 0;
    size_t                file_len  = 0;
    size_t                temp_len  = 0;
    WCHAR                *temp_path = NULL;
    DWORD                 access    = GENERIC_READ | GENERIC_WRITE;
    DWORD                 share     = 0;
    DWORD                 create    = CREATE_ALWAYS;
    DWORD                 flags     = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING;
    HANDLE                fd        = INVALID_HANDLE_VALUE;
    void                 *pagebuf   = NULL;

    // generate the output filename. note that outpath ends in a directory separator.
    path_len  = wcslen(outpath);    // count of characters, not bytes
    file_len  = wcslen(filename);   // count of characters, not bytes
    temp_len  = path_len + file_len + 1;
    temp_path = (WCHAR*) malloc(temp_len * sizeof(WCHAR));
    if (temp_path == NULL) return false;
    memset(temp_path, 0, temp_len * sizeof(WCHAR));
    wcscat(temp_path, outpath);
    wcscat(temp_path, filename);

    // create the file, and get the disk physical sector size.
    if ((fd = CreateFile(temp_path, access, share, NULL, create, flags, NULL)) == INVALID_HANDLE_VALUE)
    {   // unable to create the new file; check GetLastError().
        goto error_cleanup;
    }
    if (size > 0)
    {   // pre-allocate storage for the file contents, which should 
        // improve performance. this is best-effort, so it's not a 
        // fatal error if it fails.
        sector_sz = physical_sector_size(fd);
        file_size = align_up(size , sector_sz);
        eof.EndOfFile.QuadPart      = file_size;
        sec.AllocationSize.QuadPart = file_size;
        SetFileInformationByHandle(fd, FileAllocationInfo, &sec, sizeof(sec));
        SetFileInformationByHandle(fd, FileEndOfFileInfo , &eof, sizeof(eof));
        SetFileValidData(fd, eof.EndOfFile.QuadPart);
    }

    // figure out how many pages to write:
    GetNativeSystemInfo(&sysinfo);
    num_pages = size_t(file_size / sysinfo.dwPageSize);
    if ((pagebuf = malloc(sysinfo.dwPageSize)) == NULL)
    {   // unable to allocate the necessary memory.
        goto error_cleanup;
    }

    // start at the beginning of the file.
    SetFilePointer(fd, 0, 0, FILE_BEGIN);

    // fill each page with a random byte value and write to disk.
    for (size_t i = 0; i < num_pages; ++i)
    {
        DWORD  written = 0;
        memset(pagebuf, rand(), sysinfo.dwPageSize);
        WriteFile(fd, pagebuf , sysinfo.dwPageSize, &written, NULL);
    }

    // clean up.
    free(pagebuf);
    CloseHandle(fd);
    free(temp_path);
    return true;

error_cleanup:
    if (pagebuf != NULL) free(pagebuf);
    if (fd != INVALID_HANDLE_VALUE) CloseHandle(fd);
    if (temp_path != NULL) free(temp_path);
    return false;
}

/*///////////////////////
//  Public Functions   //
///////////////////////*/
int main(int argc, char **argv)
{
    WCHAR *pathbuf = NULL;
    WCHAR *sep     = NULL;

    if (argc < 2)
    {   // one or more required arguments are missing.
        fprintf(stderr, "ERROR: Missing argument OUTPATH.\n\n");
        print_usage();
    }
    if (!elevate_process_privileges())
    {   // unable to acquire the necessary privileges.
        fprintf(stderr, "ERROR: Unable to acquire privileges.\n");
        exit(EXIT_FAILURE);
    }
    if ((pathbuf = make_output_path(argv[1], sep)) == NULL)
    {   // unable to convert UTF-8 to UCS-2, or path doesn't exist.
        fprintf(stderr, "ERROR: Unable to convert path \'%s\'.\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    int64_t file_size = 1LL * 1024LL * 1024LL * 1024LL; // 1GB
    for (int i = 0; i < 16; ++i)
    {
        WCHAR  file[8];
        memset(file, 0, sizeof(file));
        wsprintfW(file, L"%03d.BIG", i);
        fprintf(stdout, "Writing output file %S...\n", file);
        write_output_file(pathbuf, file, file_size);
    }

    write_output_file(pathbuf, L"A.BIG", 1LL * 1024LL * 1024LL * 1024LL);
    free(pathbuf);
    exit(EXIT_SUCCESS);
}
