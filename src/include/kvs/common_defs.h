/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#ifndef __AWS_KVS_WEBRTC_COMMON_DEFINITIONS_INCLUDE__
#define __AWS_KVS_WEBRTC_COMMON_DEFINITIONS_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#ifdef KVS_PLAT_RTK_FREERTOS
#include "basic_types.h"
#include "integer.h"
#endif

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#if defined _WIN32 || defined __CYGWIN__
#if defined __GNUC__ || defined __GNUG__
#define INLINE     inline
#define LIB_EXPORT __attribute__((dllexport))
#define LIB_IMPORT __attribute__((dllimport))
#define LIB_INTERNAL
#define ALIGN_4 __attribute__((aligned(4)))
#define ALIGN_8 __attribute__((aligned(8)))
#define PACKED  __attribute__((__packed__))
#define DISCARDABLE
#else
#define INLINE     _inline
#define LIB_EXPORT __declspec(dllexport)
#define LIB_IMPORT __declspec(dllimport)
#define LIB_INTERNAL
#define ALIGN_4 __declspec(align(4))
#define ALIGN_8 __declspec(align(8))
#define PACKED
#define DISCARDABLE __declspec(selectany)
#endif
#else
#if __GNUC__ >= 4
#define INLINE       inline
#define LIB_EXPORT   __attribute__((visibility("default")))
#define LIB_IMPORT   __attribute__((visibility("default")))
#define LIB_INTERNAL __attribute__((visibility("hidden")))
#define ALIGN_4      __attribute__((aligned(4)))
#define ALIGN_8      __attribute__((aligned(8)))
#define PACKED       __attribute__((__packed__))
#define DISCARDABLE
#else
#define LIB_EXPORT
#define LIB_IMPORT
#define LIB_INTERNAL
#define ALIGN_4
#define ALIGN_8
#define DISCARDABLE
#define PACKED
#endif
#endif

#define PUBLIC_API  LIB_EXPORT
#define PRIVATE_API LIB_INTERNAL

//
// 64/32 bit check on Windows platforms
//
#if defined _WIN32 || defined _WIN64
#if defined _WIN64
#define SIZE_64
#define __LLP64__ // win64 uses LLP64 data model
#else
#define SIZE_32
#endif
#endif

//
// 64/32 bit check on GCC
//
#if defined __GNUC__ || defined __GNUG__
#if defined __x86_64__ || defined __ppc64__
#define SIZE_64
#define __LLP64__ // win64 uses LLP64 data model
#else
#define SIZE_32
#endif
#endif

#if defined _WIN32
#if defined _MSC_VER
#include <Windows.h>
#include <direct.h>
#elif defined(__MINGW64__) || defined(__MINGW32__)
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#else
#error "Unknown Windows platform!"
#endif
#define __WINDOWS_BUILD__
#ifndef _WINNT_
typedef char CHAR;
typedef short WCHAR;
typedef unsigned __int8 UINT8;
typedef __int8 INT8;
typedef unsigned __int16 UINT16;
typedef __int16 INT16;
typedef unsigned __int32 UINT32;
typedef __int32 INT32;
typedef unsigned __int64 UINT64;
typedef __int64 INT64;
typedef double DOUBLE;
typedef long double LDOUBLE;
typedef float FLOAT;
#endif

typedef double DOUBLE;
typedef long double LDOUBLE;

#elif defined(__GNUC__)

#include <stdint.h>

typedef char CHAR;
#ifndef WCHAR
typedef unsigned short WCHAR;
#endif
typedef uint8_t UINT8;
typedef int8_t INT8;
typedef uint16_t UINT16;
typedef int16_t INT16;
#ifndef UINT32
typedef uint32_t UINT32;
#endif
typedef int32_t INT32;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef double DOUBLE;
typedef long double LDOUBLE;
typedef float FLOAT;

#else

typedef char CHAR;
typedef short WCHAR;
typedef unsigned char UINT8;
typedef char INT8;
typedef unsigned short UINT16;
typedef short INT16;
typedef unsigned long UINT32;
typedef long INT32;
typedef unsigned long long UINT64;
typedef long long INT64;
typedef double DOUBLE;
typedef long double LDOUBLE;
typedef float FLOAT;

#endif

#ifndef VOID
#define VOID void
#endif

#ifdef __APPLE__
#ifndef OBJC_BOOL_DEFINED
#include "TargetConditionals.h"
#if TARGET_IPHONE_SIMULATOR
// iOS Simulator
typedef bool BOOL;
#elif TARGET_OS_IPHONE
// iOS device
typedef signed char BOOL;
#elif TARGET_OS_MAC
// Other kinds of Mac OS
typedef INT32 BOOL;
#else
#error "Unknown Apple platform"
#endif
#endif
#else
#ifndef KVS_PLAT_RTK_FREERTOS
typedef INT32 BOOL;
#endif
#endif

typedef UINT8 BYTE;
typedef VOID* PVOID;
typedef BYTE* PBYTE;
typedef BOOL* PBOOL;
typedef CHAR* PCHAR;
typedef WCHAR* PWCHAR;
typedef INT8* PINT8;
typedef UINT8* PUINT8;
typedef INT16* PINT16;
typedef UINT16* PUINT16;
typedef INT32* PINT32;
typedef UINT32* PUINT32;
typedef INT64* PINT64;
typedef UINT64* PUINT64;
typedef long LONG, *PLONG;
typedef unsigned long ULONG, *PULONG;
typedef DOUBLE* PDOUBLE;
typedef LDOUBLE* PLDOUBLE;
typedef FLOAT* PFLOAT;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

//
// int and long ptr definitions
//
#if defined SIZE_64
typedef INT64 INT_PTR, *PINT_PTR;
typedef UINT64 UINT_PTR, *PUINT_PTR;

typedef INT64 LONG_PTR, *PLONG_PTR;
typedef UINT64 ULONG_PTR, *PULONG_PTR;

#ifndef PRIu64
#ifdef __LLP64__
#define PRIu64 "llu"
#else // __LP64__
#define PRIu64 "lu"
#endif
#endif

#ifndef PRIx64
#ifdef __LLP64__
#define PRIx64 "llx"
#else // __LP64__
#define PRIx64 "lx"
#endif
#endif

#ifndef PRId64
#ifdef __LLP64__
#define PRId64 "lld"
#else // __LP64__
#define PRId64 "ld"
#endif
#endif

#elif defined SIZE_32
typedef INT32 INT_PTR, *PINT_PTR;
typedef UINT32 UINT_PTR, *PUINT_PTR;

#ifndef __int3264 // defined in Windows 'basetsd.h'
typedef INT64 LONG_PTR, *PLONG_PTR;
typedef UINT64 ULONG_PTR, *PULONG_PTR;
#endif

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#ifndef PRIx64
#define PRIx64 "llx"
#endif

#ifndef PRId64
#define PRId64 "lld"
#endif

#else
#error "Environment not 32 or 64-bit."
#endif

//
// Define pointer width size types
//
#ifndef _SIZE_T_DEFINED_IN_COMMON
#if defined(__MINGW32__)
typedef ULONG_PTR SIZE_T, *PSIZE_T;
typedef LONG_PTR SSIZE_T, *PSSIZE_T;
#elif !(defined _WIN32 || defined _WIN64)
#ifdef KVS_PLAT_RTK_FREERTOS
typedef SIZE_T* PSIZE_T;
typedef SSIZE_T* PSSIZE_T;
#else
typedef UINT_PTR SIZE_T, *PSIZE_T;
typedef INT_PTR SSIZE_T, *PSSIZE_T;
#endif
#endif
#define _SIZE_T_DEFINED_IN_COMMON
#endif

//
// Stringification macro
//
#define STR_(s) #s
#define STR(s)  STR_(s)

//
// NULL definition
//
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else /* __cplusplus */
#define NULL ((void*) 0)
#endif /* __cplusplus */
#endif /* NULL */

//
// Max and Min definitions
//
#define MAX_UINT8 ((UINT8) 0xff)
#define MAX_INT8  ((INT8) 0x7f)
#define MIN_INT8  ((INT8) 0x80)

#define MAX_UINT16 ((UINT16) 0xffff)
#define MAX_INT16  ((INT16) 0x7fff)
#define MIN_INT16  ((INT16) 0x8000)

#define MAX_UINT32 ((UINT32) 0xffffffff)
#define MAX_INT32  ((INT32) 0x7fffffff)
#define MIN_INT32  ((INT32) 0x80000000)

#define MAX_UINT64 ((UINT64) 0xffffffffffffffff)
#define MAX_INT64  ((INT64) 0x7fffffffffffffff)
#define MIN_INT64  ((INT64) 0x8000000000000000)

#ifndef STATUS
#define STATUS UINT32
#endif

//
// Some standard definitions/macros
//
#ifndef SIZEOF
#define SIZEOF(x) (sizeof(x))
#endif

//
// Check for 32/64 bit
//
#ifndef CHECK_64_BIT
#define CHECK_64_BIT (SIZEOF(SIZE_T) == 8)
#endif

#ifndef UNUSED_PARAM
#define UNUSED_PARAM(expr)                                                                                                                           \
    do {                                                                                                                                             \
        (void) (expr);                                                                                                                               \
    } while (0)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof *(array))
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)                                                                                                                               \
    do {                                                                                                                                             \
        if (p) {                                                                                                                                     \
            delete (p);                                                                                                                              \
            (p) = NULL;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)
#endif

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p)                                                                                                                         \
    do {                                                                                                                                             \
        if (p) {                                                                                                                                     \
            delete[](p);                                                                                                                             \
            (p) = NULL;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef ABS
#define ABS(a) ((a) > (0) ? (a) : (-a))
#endif

#ifndef IS_ALIGNED_TO
#define IS_ALIGNED_TO(m, n) ((m) % (n) == 0)
#endif

////////////////////////////////////////////////////
// Status definitions
////////////////////////////////////////////////////
// #TBD, error codes.
#include "kvs/error.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#if !(defined _WIN32 || defined _WIN64)
#include <unistd.h>
//#include <dirent.h>
#include <sys/time.h>
#endif

#if defined _WIN32 || defined _WIN64 || defined __CYGWIN__

// Definition of the helper stats check macros for Windows
#ifndef S_ISLNK
#define S_ISLNK(mode) FALSE
#endif //!< S_ISLNK

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) &S_IFMT) == S_IFDIR)
#endif //!< S_ISDIR

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) &S_IFMT) == S_IFREG)
#endif //!< S_ISREG

// Definition of the mkdir for Windows with 1 param
#define GLOBAL_MKDIR(p1, p2) _mkdir(p1)

// Definition of the case insensitive string compare
#define GLOBAL_STRCMPI  _strcmpi
#define GLOBAL_STRNCMPI _strnicmp

#if defined(__MINGW64__) || defined(__MINGW32__)
#define GLOBAL_RMDIR rmdir
#define GLOBAL_STAT  stat

// Typedef stat structure
typedef struct stat STAT_STRUCT;
#else //!<#if defined(__MINGW64__) || defined(__MINGW32__)
// Definition of posix APIs for Windows
#define GLOBAL_RMDIR _rmdir
#define GLOBAL_STAT  _stat

// Typedef stat structure
typedef struct _stat STAT_STRUCT;
#endif //!<#if defined(__MINGW64__) || defined(__MINGW32__)

#else //!< #if defined _WIN32 || defined _WIN64 || defined __CYGWIN__

#if !defined(__MACH__)
#ifdef KVSPIC_HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif //!< #ifdef KVSPIC_HAVE_SYS_PRCTL_H
#endif //!< #if !defined(__MACH__)

// Definition of the mkdir for non-Windows platforms with 2 params
#define GLOBAL_MKDIR(p1, p2) mkdir((p1), (p2))

// Definition of the case insensitive string compare
#define GLOBAL_STRCMPI       strcasecmp
#define GLOBAL_STRNCMPI      strncasecmp

// Definition of posix API for non-Windows platforms
#define GLOBAL_RMDIR         rmdir
#define GLOBAL_STAT          stat

// Typedef stat structure
typedef struct stat STAT_STRUCT;

#endif //!< #if defined _WIN32 || defined _WIN64 || defined __CYGWIN__

#include "allocators.h"
#include "time_port.h"
#include "thread.h"
#include "mutex.h"
#include "version.h"
#include "atomics.h"

#include "kvs_string.h"
//
// String operations
//
#define STRCAT     strcat
#define STRNCAT    strncat
#define STRCPY     strcpy
#define STRNCPY    strncpy
#define STRLEN     strlen
#define STRNLEN    strnlen
#define STRCHR     strchr
#define STRNCHR    strnchr
#define STRRCHR    strrchr
#define STRCMP     strcmp
#define STRCMPI    GLOBAL_STRCMPI
#define STRNCMPI   GLOBAL_STRNCMPI
#define STRNCMP    strncmp
#define PRINTF     printf
#define SPRINTF    sprintf
#define SNPRINTF   snprintf
#define TRIMSTRALL trimstrall
#define LTRIMSTR   ltrimstr
#define RTRIMSTR   rtrimstr
#define STRSTR     strstr
#ifdef strnstr
#define STRNSTR strnstr
#else
#define STRNSTR defaultStrnstr
#endif
#define TOLOWER    tolower
#define TOUPPER    toupper
#define TOLOWERSTR tolowerstr
#define TOUPPERSTR toupperstr

//
// Environment variables
//
#define GETENV getenv

//
// Empty string definition
//
#define EMPTY_STRING ((PCHAR) "")

//
// Check if string is empty
//
#define IS_EMPTY_STRING(str) ((str)[0] == '\0')

//
// Pseudo-random functionality
//
#ifndef SRAND
#define SRAND srand
#endif
#ifndef RAND
#define RAND rand
#endif

//
// CRT functionality
//
#define STRTOUL  strtoul
#define ULLTOSTR ulltostr
#define ULTOSTR  ultostr

//
// File operations
//
#ifndef FOPEN
#define FOPEN fopen
#endif
#ifndef FCLOSE
#define FCLOSE fclose
#endif
#ifndef FWRITE
#define FWRITE fwrite
#endif
#ifndef FPUTC
#define FPUTC fputc
#endif
#ifndef FREAD
#define FREAD fread
#endif
#ifndef FSEEK
#define FSEEK fseek
#endif
#ifndef FTELL
#define FTELL ftell
#endif
#ifndef FREMOVE
#define FREMOVE remove
#endif
#ifndef FUNLINK
#define FUNLINK unlink
#endif
#ifndef FREWIND
#define FREWIND rewind
#endif
#ifndef FRENAME
#define FRENAME rename
#endif
#ifndef FTMPFILE
#define FTMPFILE tmpfile
#endif
#ifndef FTMPNAME
#define FTMPNAME tmpnam
#endif
#ifndef FOPENDIR
#define FOPENDIR opendir
#endif
#ifndef FCLOSEDIR
#define FCLOSEDIR closedir
#endif
#ifndef FREADDIR
#define FREADDIR readdir
#endif
#ifndef FMKDIR
#define FMKDIR GLOBAL_MKDIR
#endif
#ifndef FRMDIR
#define FRMDIR GLOBAL_RMDIR
#endif
#ifndef FSTAT
#define FSTAT GLOBAL_STAT
#endif
#ifndef FSCANF
#define FSCANF fscanf
#endif

#if defined _WIN32 || defined _WIN64 || defined __CYGWIN__
#define FPATHSEPARATOR     '\\'
#define FPATHSEPARATOR_STR "\\"
#else
#define FPATHSEPARATOR     '/'
#define FPATHSEPARATOR_STR "/"
#endif
//
// String to integer conversion
//
#define STRTOUI32 strtoui32
#define STRTOI32  strtoi32
#define STRTOUI64 strtoui64
#define STRTOI64  strtoi64

#ifndef SQRT
#include <math.h>
#define SQRT sqrt
#endif

//
// Calculate the byte offset of a field in a structure of type type.
//
#ifndef FIELD_OFFSET
#define FIELD_OFFSET(type, field) ((LONG)(LONG_PTR) & (((type*) 0)->field))
#endif

//
// Aligns an integer X value up or down to alignment A
//
#define ROUND_DOWN(X, A) ((X) & ~((A) -1))
#define ROUND_UP(X, A)   (((X) + (A) -1) & ~((A) -1))

//
// Macros to swap endinanness
//
#define LOW_BYTE(x)   ((BYTE)(x))
#define HIGH_BYTE(x)  ((BYTE)(((INT16)(x) >> 8) & 0xFF))
#define LOW_INT16(x)  ((INT16)(x))
#define HIGH_INT16(x) ((INT16)(((INT32)(x) >> 16) & 0xFFFF))
#define LOW_INT32(x)  ((INT32)(x))
#define HIGH_INT32(x) ((INT32)(((INT64)(x) >> 32) & 0xFFFFFFFF))

#define MAKE_INT16(a, b) ((INT16)(((UINT8)((UINT16)(a) &0xff)) | ((UINT16)((UINT8)((UINT16)(b) &0xff))) << 8))
#define MAKE_INT32(a, b) ((INT32)(((UINT16)((UINT32)(a) &0xffff)) | ((UINT32)((UINT16)((UINT32)(b) &0xffff))) << 16))
#define MAKE_INT64(a, b) ((INT64)(((UINT32)((UINT64)(a) &0xffffffff)) | ((UINT64)((UINT32)((UINT64)(b) &0xffffffff))) << 32))

#define SWAP_INT16(x) MAKE_INT16(HIGH_BYTE(x), LOW_BYTE(x))

#define SWAP_INT32(x) MAKE_INT32(SWAP_INT16(HIGH_INT16(x)), SWAP_INT16(LOW_INT16(x)))

#define SWAP_INT64(x) MAKE_INT64(SWAP_INT32(HIGH_INT32(x)), SWAP_INT32(LOW_INT32(x)))

//
// Check if at most 1 bit is set
//
#define CHECK_POWER_2(x) !((x) & ((x) -1))

//
// Checks if only 1 bit is set
//
#define CHECK_SINGLE_BIT_SET(x) ((x) && CHECK_POWER_2(x))

//
// Handle definitions
//
#if !defined(HANDLE) && !defined(_WINNT_)
typedef UINT64 HANDLE;
#endif
#ifndef INVALID_PIC_HANDLE_VALUE
#define INVALID_PIC_HANDLE_VALUE ((UINT64) NULL)
#endif
#ifndef IS_VALID_HANDLE
#define IS_VALID_HANDLE(h) ((h) != INVALID_PIC_HANDLE_VALUE)
#endif
#ifndef POINTER_TO_HANDLE
#define POINTER_TO_HANDLE(h) ((UINT64)(h))
#endif
#ifndef HANDLE_TO_POINTER
#define HANDLE_TO_POINTER(h) ((PBYTE)(h))
#endif

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_COMMON_DEFINITIONS_INCLUDE__ */
