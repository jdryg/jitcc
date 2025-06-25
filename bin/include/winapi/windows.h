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

#if !defined(I_X86_) && !defined(_IA64_) && !defined(_AMD64_) && (defined(_X86_) && !defined(__x86_64))
#define I_X86_
#endif

#if !defined(I_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(__x86_64)
#define _AMD64_
#endif

#if !defined(I_X86_) && !(defined(_X86_) && !defined(__x86_64)) && !defined(_AMD64_) && defined(__ia64__)
#if !defined(_IA64_)
#define _IA64_
#endif
#endif

#ifndef RC_INVOKED
#include <excpt.h>
#include <stdarg.h>
#endif

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <wincon.h>
#include <winver.h>
#include <winreg.h>

#endif // _INC_WINDOWS
#endif // _WINDOWS_
