#pragma once
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "lfs.h"
// Native SDK shims only. Production structs/functions are extracted separately.
#define CONFIG_LITTLEFS_CACHE_SIZE 512
#define CONFIG_LITTLEFS_USE_MTIME 1
#define CONFIG_LITTLEFS_MTIME_USE_SECONDS 1
#define CONFIG_LITTLEFS_FD_CACHE_REALLOC_FACTOR 2
#define CONFIG_LITTLEFS_FD_CACHE_MIN_SIZE 4
#define ESP_LITTLEFS_ATTR_MTIME ((uint8_t)'t')
#define ESP_VFS_PATH_MAX 15
#define ESP_VFS_FLAG_CONTEXT_PTR 1
#define VFS_MAX_COUNT 1
#define MAX_FDS 16
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define portMAX_DELAY 0
#define __errno_r(r) (errno)
typedef int SemaphoreHandle_t;
typedef struct { int dummy; } esp_partition_t;
typedef int _lock_t;
struct _reent { int dummy; };
static unsigned locks;
static int xSemaphoreTakeRecursive(int lock, int delay) { (void)lock;(void)delay;++locks;return 1; }
static int xSemaphoreGiveRecursive(int lock) { (void)lock;assert(locks);--locks;return 1; }
static void _lock_acquire(_lock_t *lock) { (void)lock; }
static void _lock_release(_lock_t *lock) { (void)lock; }
// Only callback dispatch is native; CHECK_AND_CALL/FD release are actual IDF.
typedef struct { union { int (*close)(int); int (*close_p)(void *,int); }; } esp_vfs_fs_ops_t;
