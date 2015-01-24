/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines the DLL entry point. 
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <Windows.h>

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

/*///////////////
//   Globals   //
///////////////*/

/*///////////////////////
//   Local Functions   //
///////////////////////*/

/*///////////////////////
//  Public Functions   //
///////////////////////*/
/// @summary Implements the main DLL entry point, called automatically when the DLL is loaded.
/// @param instance The base load address of the DLL.
/// @param reason The reason for the call, indicating whether a process or a 
/// thread is attaching or detaching from the DLL.
/// @param reserved If reason is DLL_PROCESS_ATTACH, this argument is NULL for dynamic loads 
/// and non-NULL for static loads. If reason is DLL_PROCESS_DETACH, this argument is NULL if
/// FreeLibrary has been called, or non-NULL if the process is terminating.
/// @return TRUE if initialization was successful.
BOOL WINAPI DllMain(HINSTANCE /*instance*/, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            break;

        case DLL_PROCESS_DETACH:
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        default:
            break;
    }
    return TRUE;
}
