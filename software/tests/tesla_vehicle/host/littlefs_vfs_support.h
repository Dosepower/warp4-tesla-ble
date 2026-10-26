#pragma once
// Native SDK shims only for build_littlefs_vfs_harness.py. All file-hook,
// FD-cache and lfs.c bodies are extracted unmodified from the pinned SDK.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "lfs.h"
// Values from the pinned WARP4 Tesla SDK sdkconfig (LITTLEFS_* section).
#define CONFIG_LITTLEFS_CACHE_SIZE 512
#define CONFIG_LITTLEFS_USE_MTIME 1
#define CONFIG_LITTLEFS_MTIME_USE_SECONDS 1
#define CONFIG_LITTLEFS_FD_CACHE_REALLOC_FACTOR 2
#define CONFIG_LITTLEFS_FD_CACHE_MIN_SIZE 4
#define CONFIG_LITTLEFS_OBJ_NAME_LEN 64
#define CONFIG_VFS_SUPPORT_DIR 1
#define ESP_LITTLEFS_ATTR_MTIME ((uint8_t)'t')
#define ESP_LITTLEFS_TAG "esp_littlefs"
#define ESP_VFS_PATH_MAX 15
#define LOG_LOCAL_LEVEL 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define portMAX_DELAY 0
typedef int SemaphoreHandle_t;
typedef struct { int dummy; } esp_partition_t;
extern unsigned harness_locks;
static inline int xSemaphoreTakeRecursive(int lock, int delay) { (void)lock;(void)delay;++harness_locks;return 1; }
static inline int xSemaphoreGiveRecursive(int lock) { (void)lock;assert(harness_locks);--harness_locks;return 1; }
