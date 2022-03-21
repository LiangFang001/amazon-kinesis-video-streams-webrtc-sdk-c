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
#ifndef __AWS_KVS_WEBRTC_FILE_LOGGER_INCLUDE__
#define __AWS_KVS_WEBRTC_FILE_LOGGER_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/platform_utils.h"

/******************************************************************************
 * Main defines
 ******************************************************************************/
/**
 * Default values for the limits
 */
#define KVS_COMMON_FILE_INDEX_BUFFER_SIZE 256
/**
 * File based logger limit constants
 */
#define MAX_FILE_LOGGER_STRING_BUFFER_SIZE (100 * 1024 * 1024)
#define MIN_FILE_LOGGER_STRING_BUFFER_SIZE (10 * 1024)
#define MAX_FILE_LOGGER_LOG_FILE_COUNT     (10 * 1024)
/**
 * Default values used in the file logger
 */
#define FILE_LOGGER_LOG_FILE_NAME           "kvsFileLog"
#define FILE_LOGGER_LAST_INDEX_FILE_NAME    "kvsFileLogIndex"
#define FILE_LOGGER_STRING_BUFFER_SIZE      (100 * 1024)
#define FILE_LOGGER_LOG_FILE_COUNT          3
#define FILE_LOGGER_LOG_FILE_DIRECTORY_PATH "./"

/**
 * file logger declaration
 */
typedef struct {
    // string buffer. once the buffer is full, its content will be flushed to file
    PCHAR stringBuffer;

    // Size of the buffer in bytes
    // This will point to the end of the FileLogger to allow for single allocation and preserve the processor cache locality
    UINT64 stringBufferLen;

    // lock protecting the print operation
    MUTEX lock;

    // bytes starting from beginning of stringBuffer that contains valid data
    UINT64 currentOffset;

    // directory to put the log file
    CHAR logFileDir[MAX_PATH_LEN + 1];

    // file to store last log file index
    CHAR indexFilePath[MAX_PATH_LEN + 1];

    // max number of log file allowed
    UINT64 maxFileCount;

    // index for next log file
    UINT64 currentFileIndex;

    // print log to stdout too
    BOOL printLog;

    // file logger logPrint callback
    logPrintFunc fileLoggerLogPrintFn;

    // Original stored logger function
    logPrintFunc storedLoggerLogPrintFn;
} FileLogger, *PFileLogger;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * Creates a file based logger object and installs the global logger callback function
 *
 * @param - UINT64 - IN - Size of string buffer in file logger. When the string buffer is full the logger will flush everything into a new file
 * @param - UINT64 - IN - Max number of log file. When exceeded, the oldest file will be deleted when new one is generated
 * @param - PCHAR - IN - Directory in which the log file will be generated
 * @param - BOOL - IN - Whether to print log to std out too
 * @param - BOOL - IN - Whether to set global logger function pointer
 * @param - logPrintFunc* - OUT/OPT - Optional function pointer to be returned to the caller that contains the main function for actual output
 *
 * @return - STATUS code of the execution
 */
STATUS file_logger_create(UINT64, UINT64, PCHAR, BOOL, BOOL, logPrintFunc*);

/**
 * Frees the static file logger object and resets the global logging function if it was
 * previously set by the create function.
 *
 * @return - STATUS code of the execution
 */
STATUS file_logger_free();

/**
 * Flushes currentOffset number of chars from stringBuffer into logfile.
 * If maxFileCount is exceeded, the earliest file is deleted before writing to the new file.
 * After file_logger_flushToFile finishes, currentOffset is set to 0, whether the status of execution was success or not.
 *
 * @return - STATUS of execution
 */
STATUS file_logger_flushToFile();
/**
 * Helper macros to be used in pairs at the application start and end
 */
#define CREATE_DEFAULT_FILE_LOGGER()                                                                                                                 \
    file_logger_create(FILE_LOGGER_STRING_BUFFER_SIZE, FILE_LOGGER_LOG_FILE_COUNT, (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL);

#define RELEASE_FILE_LOGGER() file_logger_free();

/**
 * Helper macros to be used in pairs at the application start and end
 */
#define SET_FILE_LOGGER()                                                                                                                            \
    file_logger_create(FILE_LOGGER_STRING_BUFFER_SIZE, FILE_LOGGER_LOG_FILE_COUNT, (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL)
#define RESET_FILE_LOGGER() file_logger_free()
#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_FILE_LOGGER_INCLUDE__ */
