#ifndef _WINDOWS_
#define _WINDOWS_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef WINVER
#define WINVER 0x0502
#endif

#include <_jitcc.h>

#ifndef _INC_WINDOWS
#define _INC_WINDOWS

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <wincon.h>
#include <winver.h>
#include <winreg.h>

#endif // _INC_WINDOWS
#endif // _WINDOWS_
