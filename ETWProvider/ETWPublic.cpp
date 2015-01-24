/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the functions exported from the provider DLL. 
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////////
//   Preprocessor   //
////////////////////*/
/// Defined in evntrace.h, which requires Vista+. We want to be able to run 
/// on Windows XP without failing (even though custom events aren't supported)
/// so define the following event codes manually.
#ifndef EVENT_CONTROL_CODE_DISABLE_PROVIDER
#define EVENT_CONTROL_CODE_DISABLE_PROVIDER 0
#endif
#ifndef EVENT_CONTROL_CODE_ENABLE_PROVIDER
#define EVENT_CONTROL_CODE_ENABLE_PROVIDER  1
#endif
#ifndef EVENT_CONTROL_CODE_CAPTURE_STATE
#define EVENT_CONTROL_CODE_CAPTURE_STATE    2
#endif

/// EVNTAPI is used in evntprov.h, which is included by ETWProviderGenerated.h.
/// Define it without the DECLSPEC_IMPORT specified so that we can implement 
/// these functions locally instead of using the import library. This avoids 
/// runtime failures in the offhand chance we are running on Windows XP.
#undef  EVNTAPI
#define EVNTAPI __stdcall

/// Define the size of the output buffer when writing formatted markers.
/// This may be defined as a compile option ie. /D ETW_PROVIDER_FORMAT_BUFFER_SIZE=####.
#ifndef ETW_PROVIDER_FORMAT_BUFFER_SIZE
#define ETW_PROVIDER_FORMAT_BUFFER_SIZE     1024
#endif

/*////////////////
//   Includes   //
////////////////*/
#include <stdio.h>
#include <stdarg.h>
#include <Windows.h>
#include <sal.h>

#include "ETWProviderGenerated.h"

/*///////////////
//   Globals   //
///////////////*/
/// @summary Typedefs for use with GetProcAddress. We will load Advapi32.dll dynamically 
/// and try to resolve these functions manually. These functions are not available
/// on Windows XP, and on that platform they fall-back to no-ops.
typedef ULONG (__stdcall *EventRegisterFn)(LPCGUID, PENABLECALLBACK, PVOID, PREGHANDLE);
typedef ULONG (__stdcall *EventWriteFn)(REGHANDLE, PCEVENT_DESCRIPTOR, ULONG, PEVENT_DATA_DESCRIPTOR);
typedef ULONG (__stdcall *EventUnregisterFn)(REGHANDLE);

/// @summary Several of the API functions rely on QueryPerformanceCounter. Store the result
/// of calling QueryPerformanceFrequency here.
static LARGE_INTEGER      QPC_FREQUENCY        = { 0 };

/// @summary The value returned by TlsAlloc() used to identify the per-thread slot for tracking
/// the depth of ETWScopeEnterX() and ETWScopeExitX() pairs. This value is initialized 
/// when ETWRegisterCustomProviders() is called.
static DWORD              ETW_SCOPE_DEPTH_MAIN = TLS_OUT_OF_INDEXES;
static DWORD              ETW_SCOPE_DEPTH_TASK = TLS_OUT_OF_INDEXES;

/// @summary The following functions are resolved at runtime by dynamically loading 
/// Advapi32.dll. If running on Windows XP, they will be NULL as custom event
/// tracing is not available.
static EventWriteFn       EventWrite_Func      = NULL;
static EventRegisterFn    EventRegister_Func   = NULL;
static EventUnregisterFn  EventUnregister_Func = NULL;

/*///////////////////////
//   Local Functions   //
///////////////////////*/
/// @summary Wrapper function to call the underlying EventWrite() from Advapi32.dll, 
/// if that function is available at runtime. The function is available only
/// on Vista and later systems. If the function is not available, just return 
/// a success status and do nothing.
/// @param reghandle Registration handle of the provider. The handle comes from EventRegister.
/// @param evdesc Metadata that identifies the event to write. For details, see EVENT_DESCRIPTOR.
/// @param count Number of EVENT_DATA_DESCRIPTOR structures in UserData. The maximum number is 128.
/// @param evdata The event data to write. Allocate a block of memory that contains one or more EVENT_DATA_DESCRIPTOR structures. Set this parameter to NULL if UserDataCount is zero. The data must be in the order specified in the manifest.
/// @return ERROR_SUCCESS if the operation was successful.
static ULONG EVNTAPI EventWrite(REGHANDLE reghandle, PCEVENT_DESCRIPTOR evdesc, ULONG count, PEVENT_DATA_DESCRIPTOR evdata)
{
    if (EventWrite_Func != NULL)
    {   // This function exists in Advapi32.dll. Running on Vista+.
        return EventWrite_Func(reghandle, evdesc, count, evdata);
    }
    else return ERROR_SUCCESS;
}

/// @summary Wrapper function to call the underlying EventRegister() from Advapi32.dll, 
/// if that function is available at runtime. The function is available only
/// on Vista and later systems. If the function is not available, just return 
/// a success status and do nothing.
/// @param provider The GUID that uniquely identifies the provider.
/// @param enablecb Callback that ETW calls to notify you when a session enables or disables your provider. Can be NULL.
/// @param ctx Provider-defined context data to pass to the callback when the provider is enabled or disabled. Can be NULL.
/// @param reghandle Registration handle. The handle is used by most provider function calls. Before your provider exits, you must pass this handle to EventUnregister to free the handle.
/// @return ERROR_SUCCESS if the operation was successful.
static ULONG EVNTAPI EventRegister(LPCGUID provider, PENABLECALLBACK enablecb, PVOID ctx, PREGHANDLE reghandle)
{
    if (EventRegister_Func != NULL)
    {   // This function exists in Advapi32.dll. Running on Vista+.
        return EventRegister_Func(provider, enablecb, ctx, reghandle);
    }
    else return 0;
}

/// @summary Wrapper function to call the underlying EventUnregister() from Advapi32.dll, 
/// if that function is available at runtime. The function is available only
/// on Vista and later systems. If the function is not available, just return 
/// a success status and do nothing.
/// @param reghandle Registration handle returned by EventRegister().
/// @return ERROR_SUCCESS if the operation was successful.
static ULONG EVNTAPI EventUnregister(REGHANDLE reghandle)
{
    if (EventUnregister_Func != NULL)
    {   // This function exists in Advapi32.dll. Running on Vista+.
        return EventUnregister_Func(reghandle);
    }
    else return 0;
}

/// @summary Get a raw timestamp value from the system.
/// @return a timestamp value in unspecified units.
static inline LONGLONG timestamp(void)
{
    LARGE_INTEGER qpc = { 0 };
    QueryPerformanceCounter(&qpc);
    return qpc.QuadPart;
}

/// @summary Convert a timestamp value, or timestamp delta, to milliseconds.
/// @param raw The raw timestamp value.
/// @return the timestamp value or duration in milliseconds.
static inline float milliseconds(LONGLONG raw)
{
    LONGLONG frequency = QPC_FREQUENCY.QuadPart;
    return float(double(raw) / double(frequency)) * 1000.0f;
}

/*///////////////////////
//  Public Functions   //
///////////////////////*/
#ifdef __cplusplus
extern "C" {
#endif

/// @summary Public API function to be called to register the custom ETW providers and events.
/// This function must not be called from DllMain, or a deadlock may result.
void ETWRegisterCustomProviders(void)
{   // Call QueryPerformanceFrequency() once when the providers are registered.
    // All high-resolution timer queries rely on this frequency information.
    QueryPerformanceFrequency(&QPC_FREQUENCY);

    HMODULE advapi32 = NULL;
    // Load Advapi32.dll. This DLL is always available on XP and later, but the 
    // functions for custom ETW events are only available on Vista and later.
    if ((advapi32 = LoadLibraryW(L"Advapi32.dll")) != NULL)
    {   // Attempt to resolve the registration functions.
        EventWrite_Func      = (EventWriteFn)      GetProcAddress(advapi32, "EventWrite");
        EventRegister_Func   = (EventRegisterFn)   GetProcAddress(advapi32, "EventRegister");
        EventUnregister_Func = (EventUnregisterFn) GetProcAddress(advapi32, "EventUnregister");

        // Allocate any thread-local data slots. Don't use __declspec(thread)
        // as that can cause problems if the DLL is loaded on Windows XP.
        // The values stored at all slot indexes are automatically initialized to zero.
        ETW_SCOPE_DEPTH_MAIN = TlsAlloc();
        ETW_SCOPE_DEPTH_TASK = TlsAlloc();

        // Call the registration functions, which are defined in the 
        // ETWProviderGenerated.h file, generated by processing ETWProvider.man.
        EventRegisterETW_MAIN_THREAD();
        EventRegisterETW_TASK_THREAD();
        EventRegisterETW_USER_INPUT();
    }
}

/// @summary Public API function to be called to unregister the custom ETW providers and events.
/// This function must not be called from DllMain, or a deadlock may result.
void ETWUnregisterCustomProviders(void)
{   // Call the unregistration functions, which are defined in the 
    // ETWProviderGenerated.h file, generated by processing ETWProvider.man.
    EventUnregisterETW_USER_INPUT();
    EventUnregisterETW_TASK_THREAD();
    EventUnregisterETW_MAIN_THREAD();

    // Free any thread-local data slots.
    if (ETW_SCOPE_DEPTH_MAIN != TLS_OUT_OF_INDEXES)
    {
        TlsFree(ETW_SCOPE_DEPTH_TASK);
        ETW_SCOPE_DEPTH_TASK  = TLS_OUT_OF_INDEXES;
    }
    if (ETW_SCOPE_DEPTH_MAIN != TLS_OUT_OF_INDEXES)
    {
        TlsFree(ETW_SCOPE_DEPTH_MAIN);
        ETW_SCOPE_DEPTH_MAIN  = TLS_OUT_OF_INDEXES;
    }
}

/// @summary 
/// @param message 
/// @return 
LONGLONG ETWEnterScopeMain(char const *message)
{
    LONGLONG nowtime = timestamp();
    LPVOID raw_depth = TlsGetValue(ETW_SCOPE_DEPTH_MAIN);
    DWORD      depth = reinterpret_cast<DWORD>(raw_depth)+1;
    EventWriteMainEnterScope_Event(message, depth);
    raw_depth = reinterpret_cast<LPVOID>(depth);
    TlsSetValue(ETW_SCOPE_DEPTH_MAIN, raw_depth);
    return nowtime;
}

/// @summary 
/// @param message 
/// @param enter_time
/// @return 
LONGLONG ETWLeaveScopeMain(char const *message, LONGLONG enter_time)
{
    LONGLONG nowtime = timestamp();
    float    elapsed = milliseconds(nowtime - enter_time);
    LPVOID raw_depth = TlsGetValue(ETW_SCOPE_DEPTH_MAIN);
    DWORD      depth = reinterpret_cast<DWORD>(raw_depth)-1;
    EventWriteMainLeaveScope_Event(message, elapsed, depth);
    raw_depth = reinterpret_cast<LPVOID>(depth);
    TlsSetValue(ETW_SCOPE_DEPTH_MAIN, raw_depth);
    return nowtime;
}

/// @summary 
/// @param message
/// @return 
LONGLONG ETWEnterScopeTask(char const *message)
{
    LONGLONG nowtime = timestamp();
    LPVOID raw_depth = TlsGetValue(ETW_SCOPE_DEPTH_TASK);
    DWORD      depth = reinterpret_cast<DWORD>(raw_depth)+1;
    EventWriteTaskEnterScope_Event(message, depth);
    raw_depth = reinterpret_cast<LPVOID>(depth);
    TlsSetValue(ETW_SCOPE_DEPTH_TASK, raw_depth);
    return nowtime;
}

/// @summary 
/// @param message
/// @param enter_time
/// @return
LONGLONG ETWLeaveScopeTask(char const *message, LONGLONG enter_time)
{
    LONGLONG nowtime = timestamp();
    float    elapsed = milliseconds(nowtime - enter_time);
    LPVOID raw_depth = TlsGetValue(ETW_SCOPE_DEPTH_TASK);
    DWORD      depth = reinterpret_cast<DWORD>(raw_depth)-1;
    EventWriteTaskLeaveScope_Event(message, elapsed, depth);
    raw_depth = reinterpret_cast<LPVOID>(depth);
    TlsSetValue(ETW_SCOPE_DEPTH_TASK, raw_depth);
    return nowtime;
}

/// @summary 
/// @param thread_name
/// @param thread_id
void ETWThreadID(char const *thread_name, DWORD thread_id)
{
    EventWriteThreadID_Event(thread_name, thread_id);
}

/// @summary 
/// @param message 
void ETWMarkerMain(char const *message)
{
    EventWriteMainMarker_Event(message);
}

/// @summary 
/// @param format 
/// @param ...
void ETWMarkerFormatMain(_Printf_format_string_ char const *format, ...)
{
    char     buffer[ETW_PROVIDER_FORMAT_BUFFER_SIZE];
    va_list  arglist;
    va_start(arglist , format);
    vsprintf_s(buffer, format, arglist);
    va_end(arglist);
    EventWriteMainMarker_Event(buffer);
}

/// @summary 
/// @param buffer 
/// @param count 
/// @param format 
/// @param args 
void ETWMarkerFormatMainV(char *buffer, size_t count, char const *format, va_list args)
{   // format and make sure the buffer gets terminated.
    _vsnprintf(buffer, count , format, args);
    if (count > 0)             buffer[count-1] = '\0';
    else if (buffer != NULL)   buffer[0] = '\0';
    EventWriteMainMarker_Event(buffer);
}

/// @summary 
/// @param message 
void ETWMarkerTask(char const *message)
{
    EventWriteTaskMarker_Event(message);
}

/// @summary 
/// @param format 
/// @param ...
void ETWMarkerFormatTask(_Printf_format_string_ char const *format, ...)
{
    char     buffer[ETW_PROVIDER_FORMAT_BUFFER_SIZE];
    va_list  arglist;
    va_start(arglist , format);
    vsprintf_s(buffer, format, arglist);
    va_end(arglist);
    EventWriteTaskMarker_Event(buffer);
}

/// @summary 
/// @param buffer 
/// @param count 
/// @param format 
/// @param args 
void ETWMarkerFormatTaskV(char *buffer, size_t count, char const *format, va_list args)
{   // format and make sure the buffer gets terminated.
    _vsnprintf(buffer, count , format, args);
    if (count > 0)             buffer[count-1] = '\0';
    else if (buffer != NULL)   buffer[0] = '\0';
    EventWriteTaskMarker_Event(buffer);
}

/// @summary 
/// @param button 
/// @param flags 
/// @param x
/// @param y 
void ETWMouseDown(int button, DWORD flags, int x, int y)
{
	EventWriteMouse_down(button, flags, x, y);
}

/// @summary 
/// @param button 
/// @param flags 
/// @param x
/// @param y 
void ETWMouseUp(int button, DWORD flags, int x, int y)
{
	EventWriteMouse_up(button, flags, x, y);
}

/// @summary 
/// @param flags 
/// @param x
/// @param y
void ETWMouseMove(DWORD flags, int x, int y)
{
	EventWriteMouse_move(flags, x, y);
}

/// @summary 
/// @param flags 
/// @param delta_z 
/// @param x 
/// @param y 
void ETWMouseWheel(DWORD flags, int delta_z, int x, int y)
{
	EventWriteMouse_wheel(flags, delta_z, x, y);
}

/// @summary 
/// @param character 
/// @param name 
/// @param repeat_count 
/// @param flags 
void ETWKeyDown(DWORD character, char const* name, DWORD repeat_count, DWORD flags)
{
	EventWriteKey_down(character, name, repeat_count, flags);
}

#ifdef __cplusplus
}; /* extern "C" */
#endif