#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Minimum Windows target for keel: Windows 7 (0x0601) — required for SRWLOCK / CONDITION_VARIABLE,
// GetFinalPathNameByHandleW, and K32GetProcessMemoryInfo (PSAPI_VERSION=2).
// Never downgrade if the SDK or caller already targets Windows 8/10/11 (>= 0x0602).
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#elif _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#if !defined(WINVER) || (WINVER < _WIN32_WINNT)
#undef WINVER
#define WINVER _WIN32_WINNT
#endif
#include <windows.h>
#endif
