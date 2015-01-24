/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines the functions available to the client, as well as any 
/// utility classes to make tracing easier.
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

#ifndef ETW_CLIENT_H
#define ETW_CLIENT_H

/*////////////////
//   Includes   //
////////////////*/
#include <stdarg.h>
#include <Windows.h>
#include <sal.h>

/*////////////////////
//   Preprocessor   //
////////////////////*/
// Export or import functions based on whether ETWUser.dll is being built or referenced.
#ifdef  ETWCLIENT_EXPORTS
#define ETWCLIENT_API    __declspec(dllexport)
#else
#define ETWCLIENT_API    __declspec(dllimport)
#endif

/// @summary 
enum etw_button_e
{
    ETW_BUTTON_LEFT        = 0,
    ETW_BUTTON_MIDDLE      = 1, 
    ETW_BUTTON_RIGHT       = 2,
    ETW_BUTTON_FORCE_32BIT = 0x7FFFFFFFL
};

/// @summary 
enum etw_input_flags_e
{
    ETW_FLAGS_NONE         = (0 << 0), 
    ETW_FLAGS_DOUBLE_CLICK = (1 << 0),
    ETW_FLAGS_FORCE_32BIT  = 0xFFFFFFFFU
};

/*///////////////////////
//  Public Functions   //
///////////////////////*/
/// @summary Initializes the event tracing system. This function should be called once
/// from the primary application thread. This function MUST NOT be called from DllMain().
/// This function looks for the ETWProvider.dll file and dynamically loads it into the 
/// process address space if found. If the ETWProvider.dll file cannot be found or cannot
/// be loaded, then all ETW functions are safe to call, but no events are emitted.
ETWCLIENT_API void     ETWInitialize(void);

/// @summary Shuts down the event tracing system. This function should be called once
/// from the primary application thread. This function MUST NOT be called from DllMain().
ETWCLIENT_API void     ETWShutdown(void);

/// @summary Emits an event specifying the name associated with a given thread ID. Typically,
/// this function would be called when a new thread is created.
/// @param thread_name A NULL-terminated string identifying the thread.
/// @param thread_id The operating system identifier of the thread.
ETWCLIENT_API void     ETWThreadID(char const *thread_name, DWORD thread_id);

/// @summary Indicates that a named, timed scope is being entered. Typically, this function
/// is not called directly; instead, it is easier and safer to use the ETWScope class.
/// @param message A NULL-terminated string identifying the scope.
/// @return The current timestamp, which must be passed to ETWLeaveScope.
ETWCLIENT_API LONGLONG ETWEnterScopeMain(char const *message);

/// @summary Indicates that a named, timed scope is being exited. Typically, this function 
/// is not called directly; instead, it is easier and safer to use the ETWScope class.
/// @param message A NULL-terminated string identifying the scope.
/// @param enter_time The timestamp value returned from ETWEnterScope().
/// @return The current timestamp.
ETWCLIENT_API LONGLONG ETWLeaveScopeMain(char const *message, LONGLONG enter_time);

/// @summary Emits a string marker event to the tracing system.
/// @param message A NULL-terminated string to emit to the tracing system.
ETWCLIENT_API void     ETWMarkerMain(char const *message);

/// @summary Emits a formatted string marker event to the tracing system.
/// @param format A NULL-terminated string following printf format specifier rules.
/// @param ... Substitution arguments for the format string.
ETWCLIENT_API void     ETWMarkerFormatMain(_Printf_format_string_ char const *format, ...);

/// @summary Indicates that a named, timed scope is being entered. Typically, this function
/// is not called directly; instead, it is easier and safer to use the ETWScope class.
/// @param message A NULL-terminated string identifying the scope.
/// @return The current timestamp, which must be passed to ETWLeaveScope.
ETWCLIENT_API LONGLONG ETWEnterScopeTask(char const *message);

/// @summary Indicates that a named, timed scope is being exited. Typically, this function 
/// is not called directly; instead, it is easier and safer to use the ETWScope class.
/// @param message A NULL-terminated string identifying the scope.
/// @param enter_time The timestamp value returned from ETWEnterScope().
/// @return The current timestamp.
ETWCLIENT_API LONGLONG ETWLeaveScopeTask(char const *message, LONGLONG enter_time);

/// @summary Emits a string marker event to the tracing system.
/// @param message A NULL-terminated string to emit to the tracing system.
ETWCLIENT_API void     ETWMarkerTask(char const *message);

/// @summary Emits a formatted string marker event to the tracing system.
/// @param format A NULL-terminated string following printf format specifier rules.
/// @param ... Substitution arguments for the format string.
ETWCLIENT_API void     ETWMarkerFormatTask(_Printf_format_string_ char const *format, ...);

/// @summary Emits a mouse button press event to the tracing system.
/// @param button One of the values of etw_button_e.
/// @param flags A combination of one or more values of etw_input_flags_e.
/// @param x The x-coordinate of the mouse cursor.
/// @param y The y-coordinate of the mouse cursor.
ETWCLIENT_API void     ETWMouseDown(int button, DWORD flags, int x, int y);

/// @summary Emits a mouse button release event to the tracing system.
/// @param button One of the values of etw_button_e.
/// @param flags A combination of one or more values of etw_input_flags_e.
/// @param x The x-coordinate of the mouse cursor.
/// @param y The y-coordinate of the mouse cursor.
ETWCLIENT_API void     ETWMouseUp(int button, DWORD flags, int x, int y);

/// @summary Emits a mouse move event to the tracing system.
/// @param flags A combination of one or more values of etw_input_flags_e.
/// @param x The x-coordinate of the mouse cursor.
/// @param y The y-coordinate of the mouse cursor.
ETWCLIENT_API void     ETWMouseMove(DWORD flags, int x, int y);

/// @summary Emits a mouse wheel move event to the tracing system.
/// @param flags A combination of one or more values of etw_input_flags_e.
/// @param delta_z The amount of movement on the z-axis (mouse wheel).
/// @param x The x-coordinate of the mouse cursor.
/// @param y The y-coordinate of the mouse cursor.
ETWCLIENT_API void     ETWMouseWheel(DWORD flags, int delta_z, int x, int y);

/// @summary Emits a key press event to the tracing system.
/// @param character The raw character code of the key that was pressed.
/// @param name A NULL-terminated spring specifying a name for the pressed key.
/// @param repeat_count The number of key repeats that have occurred for this key.
/// @param flags A combination of one or more values of etw_input_flags_e.
ETWCLIENT_API void     ETWKeyDown(DWORD character, char const* name, DWORD repeat_count, DWORD flags);

#ifdef __cplusplus
/// @summary A helper class to manage entering and exiting a scope. This 
/// class calls ETWEnterScopeMain for your when it is instantiated, and 
/// automatically calls ETWLeaveScopeMain when it is destroyed.
class ETWMainScope
{
public:
    inline ETWMainScope(char const *name)
        :
        Description(name)
    {
        EnterTime = ETWEnterScopeMain(name);
    }

    inline ~ETWMainScope(void)
    {
        ETWLeaveScopeMain(Description, EnterTime);
    }
private:
    ETWMainScope(void);                             /* disallow default */
    ETWMainScope(ETWMainScope const &);             /* disallow copying */
    ETWMainScope& operator =(ETWMainScope const &); /* disallow copying */
private:
    char const *Description;
    LONGLONG    EnterTime;
};

/// @summary A helper class to manage entering and exiting a scope. This 
/// class calls ETWEnterScopeTask for your when it is instantiated, and 
/// automatically calls ETWLeaveScopeTask when it is destroyed.
class ETWTaskScope
{
public:
    inline ETWTaskScope(char const *name)
        :
        Description(name)
    {
        EnterTime = ETWEnterScopeTask(name);
    }

    inline ~ETWTaskScope(void)
    {
        ETWLeaveScopeTask(Description, EnterTime);
    }
private:
    ETWTaskScope(void);                             /* disallow default */
    ETWTaskScope(ETWTaskScope const &);             /* disallow copying */
    ETWTaskScope& operator =(ETWTaskScope const &); /* disallow copying */
private:
    char const *Description;
    LONGLONG    EnterTime;
};
#endif /*  defined(__cplusplus) */

#endif /* !defined(ETW_CLIENT_H) */
