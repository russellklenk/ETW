/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines the DLL entry point. 
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <tchar.h>
#include <assert.h>
#include "ETWClient.h"

/*/////////////////
//   Constants   //
/////////////////*/
// Define the size of the output buffer when writing formatted markers.
// This may be defined as a compile option ie. /D ETW_PROVIDER_FORMAT_BUFFER_SIZE=####.
#ifndef ETW_PROVIDER_FORMAT_BUFFER_SIZE
#define ETW_PROVIDER_FORMAT_BUFFER_SIZE     1024
#endif

// Shut up compiler warnings about unused local parameters.
#define UNUSED_ARG(x)                 \
    do {                              \
        (x);                          \
    __pragma(warning(push));          \
    __pragma(warning(disable:4127));  \
        } while(0);                   \
    __pragma(warning(pop))

// Resolve a function pointer from ETWProvider.dll, and set the function 
// pointer to the stub function if it can't be dynamically loaded. For this
// to work, you must follow some naming conventions. Given function name:
// 
// name = ETWFoo (without quotes)
//
// The function pointer (typedef) should be: ETWFooFn
// The global function pointer should be:    ETWFoo_Func
// The stub/no-op function name should be:   ETWFoo_Stub
// The resolve call in ETWInitialize() is:   ETW_DLL_RESOLVE(dll_inst, ETWFoo);
#define ETW_DLL_RESOLVE(dll, fname)                                \
    do {                                                           \
        fname##_Func = (fname##Fn) GetProcAddress(dll, #fname);    \
        if (fname##_Func == NULL) { fname##_Func = fname##_Stub; } \
    __pragma(warning(push));                                       \
    __pragma(warning(disable:4127));                               \
        } while(0);                                                \
    __pragma(warning(pop))

/*///////////////
//   Globals   //
///////////////*/
// Function pointer typedefs for the functions exported by ETWProvider.dll.
typedef void     (__cdecl *ETWRegisterCustomProvidersFn)(void);
typedef void     (__cdecl *ETWUnregisterCustomProvidersFn)(void);
typedef void     (__cdecl *ETWThreadIDFn)(char const*, DWORD);
typedef void     (__cdecl *ETWMarkerMainFn)(char const*);
typedef void     (__cdecl *ETWMarkerFormatMainVFn)(char*, size_t, char const*, va_list);
typedef void     (__cdecl *ETWMarkerTaskFn)(char const*);
typedef void     (__cdecl *ETWMarkerFormatTaskVFn)(char*, size_t, char const*, va_list);
typedef void     (__cdecl *ETWMouseDownFn)(int, DWORD, int, int);
typedef void     (__cdecl *ETWMouseUpFn)(int, DWORD, int, int);
typedef void     (__cdecl *ETWMouseMoveFn)(DWORD, int, int);
typedef void     (__cdecl *ETWMouseWheelFn)(DWORD, int, int, int);
typedef void     (__cdecl *ETWKeyDownFn)(DWORD, char const*, DWORD, DWORD);
typedef LONGLONG (__cdecl *ETWEnterScopeMainFn)(char const*);
typedef LONGLONG (__cdecl *ETWLeaveScopeMainFn)(char const*, LONGLONG);
typedef LONGLONG (__cdecl *ETWEnterScopeTaskFn)(char const*);
typedef LONGLONG (__cdecl *ETWLeaveScopeTaskFn)(char const*, LONGLONG);

// Global pointers to the functions we load from ETWProvider.dll. If the DLL
// cannot be loaded, these will be set to no-op stubs after ETWInitialize() returns.
static ETWRegisterCustomProvidersFn   ETWRegisterCustomProviders_Func   = NULL;
static ETWUnregisterCustomProvidersFn ETWUnregisterCustomProviders_Func = NULL;
static ETWThreadIDFn                  ETWThreadID_Func                  = NULL;
static ETWMarkerMainFn                ETWMarkerMain_Func                = NULL;
static ETWMarkerFormatMainVFn         ETWMarkerFormatMainV_Func         = NULL;
static ETWMarkerTaskFn                ETWMarkerTask_Func                = NULL;
static ETWMarkerFormatTaskVFn         ETWMarkerFormatTaskV_Func         = NULL;
static ETWEnterScopeMainFn            ETWEnterScopeMain_Func            = NULL;
static ETWLeaveScopeMainFn            ETWLeaveScopeMain_Func            = NULL;
static ETWEnterScopeTaskFn            ETWEnterScopeTask_Func            = NULL;
static ETWLeaveScopeTaskFn            ETWLeaveScopeTask_Func            = NULL;
static ETWMouseDownFn                 ETWMouseDown_Func                 = NULL;
static ETWMouseUpFn                   ETWMouseUp_Func                   = NULL;
static ETWMouseMoveFn                 ETWMouseMove_Func                 = NULL;
static ETWMouseWheelFn                ETWMouseWheel_Func                = NULL;
static ETWKeyDownFn                   ETWKeyDown_Func                   = NULL;
static HMODULE                        ETWProviderDLL                    = NULL;

/*///////////////////////
//   Local Functions   //
///////////////////////*/
static void __cdecl ETWRegisterCustomProviders_Stub(void)
{
    /* empty */
}

static void __cdecl ETWUnregisterCustomProviders_Stub(void)
{
    /* empty */
}

static void __cdecl ETWThreadID_Stub(char const *thread_name, DWORD thread_id)
{
    UNUSED_ARG(thread_name);
    UNUSED_ARG(thread_id);
}

static void __cdecl ETWMarkerMain_Stub(char const *message)
{
    UNUSED_ARG(message);
}

static void __cdecl ETWMarkerFormatMainV_Stub(char *buffer, size_t count, char const *format, va_list args)
{
    UNUSED_ARG(buffer);
    UNUSED_ARG(count);
    UNUSED_ARG(format);
    UNUSED_ARG(args);
}

static void __cdecl ETWMarkerTask_Stub(char const *message)
{
    UNUSED_ARG(message);
}

static void __cdecl ETWMarkerFormatTaskV_Stub(char *buffer, size_t count, char const *format, va_list args)
{
    UNUSED_ARG(buffer);
    UNUSED_ARG(count);
    UNUSED_ARG(format);
    UNUSED_ARG(args);
}

static void __cdecl ETWMouseDown_Stub(int button, DWORD flags, int x, int y)
{
    UNUSED_ARG(button);
    UNUSED_ARG(flags);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
}

static void __cdecl ETWMouseUp_Stub(int button, DWORD flags, int x, int y)
{
    UNUSED_ARG(button);
    UNUSED_ARG(flags);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
}

static void __cdecl ETWMouseMove_Stub(DWORD flags, int x, int y)
{
    UNUSED_ARG(flags);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
}

static void __cdecl ETWMouseWheel_Stub(DWORD flags, int delta_z, int x, int y)
{
    UNUSED_ARG(flags);
    UNUSED_ARG(delta_z);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
}

static void __cdecl ETWKeyDown_Stub(DWORD character, char const *name, DWORD repeat_count, DWORD flags)
{
    UNUSED_ARG(character);
    UNUSED_ARG(name);
    UNUSED_ARG(repeat_count);
    UNUSED_ARG(flags);
}

static LONGLONG __cdecl ETWEnterScopeMain_Stub(char const *message)
{
    UNUSED_ARG(message);
    return 0;
}

static LONGLONG __cdecl ETWLeaveScopeMain_Stub(char const *message, LONGLONG enter_time)
{
    UNUSED_ARG(message);
    UNUSED_ARG(enter_time);
    return 0;
}

static LONGLONG __cdecl ETWEnterScopeTask_Stub(char const *message)
{
    UNUSED_ARG(message);
    return 0;
}

static LONGLONG __cdecl ETWLeaveScopeTask_Stub(char const *message, LONGLONG enter_time)
{
    UNUSED_ARG(message);
    UNUSED_ARG(enter_time);
    return 0;
}

/*///////////////////////
//  Public Functions   //
///////////////////////*/
void ETWInitialize(void)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    // ETWProvider.dll is copied to %TEMP% when it is registered, by registeretw.cmd,
    // so look for it there first; otherwise, fall back to LoadLibrary search paths.
    static TCHAR const *ETW_PROVIDER_DLL_PATH = _T("%TEMP%\\ETWProvider.dll");

    HMODULE dll_inst = NULL;
    TCHAR *dll_path = NULL;
    DWORD  path_len = 0;
    if ((path_len = ExpandEnvironmentStrings(ETW_PROVIDER_DLL_PATH, NULL, 0)) == 0)
    {   // ExpandEnvironmentStrings() failed. use the stub functions.
        // call GetLastError() if you want to know the specific code.
        goto use_stubs;
    }

    // allocate a buffer large enough for the full path.
    // ExpandEnvironmentStrings() returns the necessary length, in *characters*, 
    // and including the terminating null character, but MSDN says that for ANSI
    // strings, the buffer size should include space for one extra zero byte, 
    // and it isn't clear from the docs whether path_len includes this...
    if ((dll_path = (TCHAR*)malloc((path_len + 1) * sizeof(TCHAR))) == NULL)
    {   // unable to allocate the necessary memory; fail. check errno.
        goto use_stubs;
    }
    if ((path_len = ExpandEnvironmentStrings(ETW_PROVIDER_DLL_PATH, dll_path, path_len + 1)) == 0)
    {   // for some reason now we can't perform the expansion.
        // call GetLastError() if you want to know the specific code.
        goto cleanup_and_use_stubs;
    }

    // now attempt to load ETWProvider.dll from the specific location.
    if ((dll_inst = LoadLibrary(dll_path)) == NULL)
    {   // if that didn't work, check the LoadLibrary search paths.
        if ((dll_inst = LoadLibrary(_T("ETWProvider.dll"))) == NULL)
        {   // can't find it, so use the stubs.
            goto cleanup_and_use_stubs;
        }
    }

    // the DLL was loaded from somewhere, so resolve functions.
    // we could be loading an older version of the DLL, so it's possible 
    // that some functions are available while others are not.
    // the macro will set any missing functions to the stub implementation.
    ETW_DLL_RESOLVE(dll_inst, ETWRegisterCustomProviders);
    ETW_DLL_RESOLVE(dll_inst, ETWUnregisterCustomProviders);
    ETW_DLL_RESOLVE(dll_inst, ETWThreadID);
    ETW_DLL_RESOLVE(dll_inst, ETWMarkerMain);
    ETW_DLL_RESOLVE(dll_inst, ETWMarkerFormatMainV);
    ETW_DLL_RESOLVE(dll_inst, ETWEnterScopeMain);
    ETW_DLL_RESOLVE(dll_inst, ETWLeaveScopeMain);
    ETW_DLL_RESOLVE(dll_inst, ETWMarkerTask);
    ETW_DLL_RESOLVE(dll_inst, ETWMarkerFormatTaskV);
    ETW_DLL_RESOLVE(dll_inst, ETWEnterScopeTask);
    ETW_DLL_RESOLVE(dll_inst, ETWLeaveScopeTask);
    ETW_DLL_RESOLVE(dll_inst, ETWMouseDown);
    ETW_DLL_RESOLVE(dll_inst, ETWMouseUp);
    ETW_DLL_RESOLVE(dll_inst, ETWMouseMove);
    ETW_DLL_RESOLVE(dll_inst, ETWMouseWheel);
    ETW_DLL_RESOLVE(dll_inst, ETWKeyDown);

    // register the custom providers as part of the initialization.
    ETWRegisterCustomProviders_Func();

    // done with everything, so clean up.
    free(dll_path);  dll_path = NULL;
    ETWProviderDLL = dll_inst;
    return;

cleanup_and_use_stubs:
    if (dll_inst != NULL) FreeLibrary(dll_inst);
    if (dll_path != NULL) free(dll_path);
    /* fallthrough */

use_stubs:
    ETWRegisterCustomProviders_Func   = ETWRegisterCustomProviders_Stub;
    ETWUnregisterCustomProviders_Func = ETWUnregisterCustomProviders_Stub;
    ETWThreadID_Func                  = ETWThreadID_Stub;
    ETWMarkerMain_Func                = ETWMarkerMain_Stub;
    ETWMarkerFormatMainV_Func         = ETWMarkerFormatMainV_Stub;
    ETWEnterScopeMain_Func            = ETWEnterScopeMain_Stub;
    ETWLeaveScopeMain_Func            = ETWLeaveScopeMain_Stub;
    ETWMarkerTask_Func                = ETWMarkerTask_Stub;
    ETWMarkerFormatTaskV_Func         = ETWMarkerFormatTaskV_Stub;
    ETWEnterScopeTask_Func            = ETWEnterScopeTask_Stub;
    ETWLeaveScopeTask_Func            = ETWLeaveScopeTask_Stub;
    ETWMouseDown_Func                 = ETWMouseDown_Stub;
    ETWMouseUp_Func                   = ETWMouseUp_Stub;
    ETWMouseMove_Func                 = ETWMouseMove_Stub;
    ETWMouseWheel_Func                = ETWMouseWheel_Stub;
    ETWKeyDown_Func                   = ETWKeyDown_Stub;
#else
    /* empty */
#endif
}

void ETWShutdown(void)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    // unregister the custom providers; no more custom events will be visible.
    ETWUnregisterCustomProviders_Func();

    // point all of the function pointers at the local stubs for safety.
    ETWRegisterCustomProviders_Func   = ETWRegisterCustomProviders_Stub;
    ETWUnregisterCustomProviders_Func = ETWUnregisterCustomProviders_Stub;
    ETWThreadID_Func                  = ETWThreadID_Stub;
    ETWMarkerMain_Func                = ETWMarkerMain_Stub;
    ETWMarkerFormatMainV_Func         = ETWMarkerFormatMainV_Stub;
    ETWEnterScopeMain_Func            = ETWEnterScopeMain_Stub;
    ETWLeaveScopeMain_Func            = ETWLeaveScopeMain_Stub;
    ETWMarkerTask_Func                = ETWMarkerTask_Stub;
    ETWMarkerFormatTaskV_Func         = ETWMarkerFormatTaskV_Stub;
    ETWEnterScopeTask_Func            = ETWEnterScopeTask_Stub;
    ETWLeaveScopeTask_Func            = ETWLeaveScopeTask_Stub;
    ETWMouseDown_Func                 = ETWMouseDown_Stub;
    ETWMouseUp_Func                   = ETWMouseUp_Stub;
    ETWMouseMove_Func                 = ETWMouseMove_Stub;
    ETWMouseWheel_Func                = ETWMouseWheel_Stub;
    ETWKeyDown_Func                   = ETWKeyDown_Stub;

    // unload the DLL, which should only have one reference.
    if (ETWProviderDLL != NULL)
    {
        FreeLibrary(ETWProviderDLL);
        ETWProviderDLL = NULL;
    }
#else
    /* empty */
#endif
}

LONGLONG ETWEnterScopeMain(char const *message)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWEnterScopeMain_Func && "ETWInitialize must be called!");
    return ETWEnterScopeMain_Func(message);
#else
    UNUSED_ARG(message);
    return 0;
#endif
}

LONGLONG ETWLeaveScopeMain(char const *message, LONGLONG enter_time)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWLeaveScopeMain_Func && "ETWInitialize must be called!");
    return ETWLeaveScopeMain_Func(message, enter_time);
#else
    UNUSED_ARG(message);
    UNUSED_ARG(enter_time);
    return 0;
#endif
}

LONGLONG ETWEnterScopeTask(char const *message)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWEnterScopeTask_Func && "ETWInitialize must be called!");
    return ETWEnterScopeTask_Func(message);
#else
    UNUSED_ARG(message);
    return 0;
#endif
}

LONGLONG ETWLeaveScopeTask(char const *message, LONGLONG enter_time)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWLeaveScopeTask_Func && "ETWInitialize must be called!");
    return ETWLeaveScopeTask_Func(message, enter_time);
#else
    UNUSED_ARG(message);
    UNUSED_ARG(enter_time);
    return 0;
#endif
}

void ETWThreadID(char const *thread_name, DWORD thread_id)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWThreadID_Func && "ETWInitialize must be called!");
    ETWThreadID_Func(thread_name, thread_id);
#else
    UNUSED_ARG(thread_name);
    UNUSED_ARG(thread_id);
    return 0;
#endif
}

void ETWMarkerMain(char const *message)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMarkerMain_Func && "ETWInitialize must be called!");
    ETWMarkerMain_Func(message);
#else
    UNUSED_ARG(message);
#endif
}

void ETWMarkerFormatMain(_Printf_format_string_ char const *format, ...)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMarkerFormatMainV_Func && "ETWInitialize must be called!");
    char   buffer[ETW_PROVIDER_FORMAT_BUFFER_SIZE];
    va_list  args;
    va_start(args, format);
    // NOTE: the second argument expects the buffer length in characters.
    ETWMarkerFormatMainV_Func(buffer, ETW_PROVIDER_FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
#else
    UNUSED_ARG(format);
#endif
}

void ETWMarkerTask(char const *message)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMarkerTask_Func && "ETWInitialize must be called!");
    ETWMarkerTask_Func(message);
#else
    UNUSED_ARG(message);
#endif
}

void ETWMarkerFormatTask(_Printf_format_string_ char const *format, ...)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMarkerFormatTaskV_Func && "ETWInitialize must be called!");
    char   buffer[ETW_PROVIDER_FORMAT_BUFFER_SIZE];
    va_list  args;
    va_start(args, format);
    // NOTE: the second argument expects the buffer length in characters.
    ETWMarkerFormatTaskV_Func(buffer, ETW_PROVIDER_FORMAT_BUFFER_SIZE, format, args);
    va_end(args);
#else
    UNUSED_ARG(format);
#endif
}

void ETWMouseDown(int button, DWORD flags, int x, int y)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMouseDown_Func && "ETWInitialize must be called!");
    ETWMouseDown_Func(button, flags, x, y);
#else
    UNUSED_ARG(button);
    UNUSED_ARG(flags);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
#endif
}

void ETWMouseUp(int button, DWORD flags, int x, int y)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMouseUp_Func && "ETWInitialize must be called!");
    ETWMouseUp_Func(button, flags, x, y);
#else
    UNUSED_ARG(button);
    UNUSED_ARG(flags);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
#endif
}

void ETWMouseMove(DWORD flags, int x, int y)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMouseMove_Func && "ETWInitialize must be called!");
    ETWMouseMove_Func(flags, x, y);
#else
    UNUSED_ARG(flags);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
#endif
}

void ETWMouseWheel(DWORD flags, int delta_z, int x, int y)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWMouseWheel_Func && "ETWInitialize must be called!");
    ETWMouseWheel_Func(flags, delta_z, x, y);
#else
    UNUSED_ARG(flags);
    UNUSED_ARG(delta_z);
    UNUSED_ARG(x);
    UNUSED_ARG(y);
#endif
}

void ETWKeyDown(DWORD character, char const* name, DWORD repeat_count, DWORD flags)
{
#ifndef ETW_STRIP_IMPLEMENTATION
    assert(ETWKeyDown_Func && "ETWInitialize must be called!");
    ETWKeyDown_Func(character, name, repeat_count, flags);
#else
    UNUSED_ARG(character);
    UNUSED_ARG(name);
    UNUSED_ARG(repeat_count);
    UNUSED_ARG(flags);
#endif
}
