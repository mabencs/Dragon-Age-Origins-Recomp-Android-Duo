// The FidelityFX SDK's host code built for Android (PLAN 9.4): the few Microsoft C
// runtime functions it calls - wcscpy_s, strcpy_s, wcstombs_s, sprintf_s, swprintf_s and
// _countof - in terms of the standard ones. Forced into the SDK's sources only
// (app/src/main/cpp/CMakeLists.txt, warden_ffx).
#pragma once

// The SDK's sources ask Vulkan for the device's extensions to choose their shader paths;
// the call goes to warden_fsr3.cc instead, which answers the ones the device has enabled
// for them (none): before vulkan.h, so its declaration is renamed with it.
#define vkEnumerateDeviceExtensionProperties warden_ffx_vkEnumerateDeviceExtensionProperties

#ifdef __cplusplus
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <locale>  // std::wstring_convert, which its non-Windows name conversion uses

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

// ffx_message.cpp's non-Windows path uses it without including ffx_util.h, which defines
// it exactly so.
#define FFX_UNUSED(x) ((void)(x))

inline int wcscpy_s(wchar_t* dest, size_t size, const wchar_t* src) {
  if (!dest || !size) {
    return 22;  // EINVAL
  }
  if (!src) {
    dest[0] = 0;
    return 22;
  }
  std::wcsncpy(dest, src, size - 1);
  dest[size - 1] = 0;
  return 0;
}

template <size_t N>
inline int wcscpy_s(wchar_t (&dest)[N], const wchar_t* src) {
  return wcscpy_s(dest, N, src);
}

inline int strcpy_s(char* dest, size_t size, const char* src) {
  if (!dest || !size) {
    return 22;
  }
  if (!src) {
    dest[0] = 0;
    return 22;
  }
  std::strncpy(dest, src, size - 1);
  dest[size - 1] = 0;
  return 0;
}

template <size_t N>
inline int strcpy_s(char (&dest)[N], const char* src) {
  return strcpy_s(dest, N, src);
}

// Converts at most `count` characters into `dest` (of `size` bytes), always terminated;
// *converted is the bytes written, the terminator included.
inline int wcstombs_s(size_t* converted, char* dest, size_t size, const wchar_t* src,
                      size_t count) {
  if (converted) {
    *converted = 0;
  }
  if (!dest || !size || !src) {
    return 22;
  }
  const size_t limit = count < size - 1 ? count : size - 1;
  const size_t written = std::wcstombs(dest, src, limit);
  if (written == static_cast<size_t>(-1)) {
    dest[0] = 0;
    return 42;  // EILSEQ
  }
  dest[written < size ? written : size - 1] = 0;
  if (converted) {
    *converted = written + 1;
  }
  return 0;
}

inline int sprintf_s(char* buffer, size_t size, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const int written = std::vsnprintf(buffer, size, format, arguments);
  va_end(arguments);
  return written;
}

inline int swprintf_s(wchar_t* buffer, size_t size, const wchar_t* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const int written = std::vswprintf(buffer, size, format, arguments);
  va_end(arguments);
  return written;
}
#endif  // __cplusplus
