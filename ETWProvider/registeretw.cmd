@REM This script copies the ETWProvider.dll file to %TEMP% and then 
@REM registers the custom providers with Event Tracing for Windows so
@REM that custom events can be displayed in the UI. Any existing copy
@REM of ETWProvider.dll is unregistered.

@ECHO OFF

SET SCRIPT_ROOT=%~dp0

@REM Set the list of locations to check for ETWProvider.dll. 
@REM Typically, on a deployed system, it will be found in the same 
@REM directory as this file, but during development, we also want to
@REM check the Debug and Release build output directories.

SET ETWProviderDLL0="%SCRIPT_ROOT%ETWProvider.dll"
SET ETWProviderDLL1="%SCRIPT_ROOT%..\Release\ETWProvider.dll"
SET ETWProviderDLL2="%SCRIPT_ROOT%..\Debug\ETWProvider.dll"

SET ETWProviderDLL=%ETWProviderDLL0%
IF EXIST %ETWProviderDLL% ( 
    GOTO FoundDLL 
)
SET ETWProviderDLL=%ETWProviderDLL1%
IF EXIST %ETWProviderDLL% (
    GOTO FoundDLL
)
SET ETWProviderDLL=%ETWProviderDLL2%
IF EXIST %ETWProviderDLL% (
    GOTO FoundDLL
) ELSE (
    GOTO NoDLL
)

:FoundDLL
@REM Set the list of locations to check for ETWProvider.man. 
@REM Typically, on a deployed system, it will be found in the same 
@REM directory as this file, but during development, we also want to
@REM check the Debug and Release build output directories.

ECHO Found ETWProvider.dll at %ETWProviderDLL%.

SET ETWProviderMAN0="%SCRIPT_ROOT%ETWProvider.man"
SET ETWProviderMAN1="%SCRIPT_ROOT%..\Debug\ETWProvider.man"
SET ETWProviderMAN2="%SCRIPT_ROOT%..\Release\ETWProvider.man"

SET ETWProviderMAN=%ETWProviderMAN0%
IF EXIST %ETWProviderMAN% ( 
    GOTO FoundMAN 
)
SET ETWProviderMAN=%ETWProviderMAN1%
IF EXIST %ETWProviderMAN% (
    GOTO FoundMAN
)
SET ETWProviderMAN=%ETWProviderMAN2%
IF EXIST %ETWProviderMAN% (
    GOTO FoundMAN
) ELSE (
    GOTO NoMAN
)

:FoundMAN
ECHO Found ETWProvider.man at %ETWProviderMAN%.
ECHO Registering custom ETW providers...
XCOPY /y %ETWProviderDLL% %TEMP%
WEVTUTIL UM %ETWProviderMAN%
IF %ERRORLEVEL% == 5 (
    GOTO NoPERM
)
WEVTUTIL IM %ETWProviderMAN%
ECHO Custom ETW providers registered successfully.
EXIT /b 

:NoDLL
ECHO ERROR: The file ETWProvider.dll could not be found. Copy it to this directory and run registeretw.cmd again.
EXIT /b

:NoMAN
ECHO ERROR: The file ETWProvider.man could not be found. Copy it to this directory and run registeretw.cmd again.
EXIT /b

:NoPERM
ECHO ERROR: This script must be run as an administrator. Run cmd.exe as an Administrator and run registeretw.cmd again.
EXIT /b
