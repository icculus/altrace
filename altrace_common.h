/**
 * alTrace; a debugging tool for OpenAL.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_ALTRACE_COMMON_H_
#define _INCL_ALTRACE_COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <assert.h>

#include "AL/al.h"
#include "AL/alc.h"

#define ALTRACE_VERSION "0.0.1"

#define ALTRACE_LOG_FILE_MAGIC  0x0104E5A1
#define ALTRACE_LOG_FILE_FORMAT 1

/* AL_EXT_FLOAT32 support... */
#ifndef AL_FORMAT_MONO_FLOAT32
#define AL_FORMAT_MONO_FLOAT32 0x10010
#endif

#ifndef AL_FORMAT_STEREO_FLOAT32
#define AL_FORMAT_STEREO_FLOAT32 0x10011
#endif

/* ALC_EXT_DISCONNECTED support... */
#ifndef ALC_CONNECTED
#define ALC_CONNECTED 0x313
#endif

#ifdef __cplusplus
extern "C" {
#endif

// to be filled in by apps.
extern const char *GAppName;

typedef uint8_t uint8;
typedef int16_t int16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef unsigned int uint;

#if defined(__clang__) || defined(__GNUC__)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

#ifdef __linux__
#  include <endian.h>
#  if __BYTE_ORDER == 4321
#    define BIGENDIAN 1
#  endif
#elif defined(__hppa__) || defined(__m68k__) || defined(mc68000) || \
    defined(_M_M68K) || (defined(__MIPS__) && defined(__MISPEB__)) || \
    defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
    defined(__sparc__)
#  define BIGENDIAN 1
#endif

#ifdef BIGENDIAN
static uint32 swap32(const uint32 x)
{
    return (uint32) ( (x << 24) | ((x << 8) & 0x00FF0000) |
                      ((x >> 8) & 0x0000FF00) | (x >> 24) );
}

static uint64 swap64(uint64 x)
{
    uint32 hi, lo;
    lo = (Uint32) (x & 0xFFFFFFFF);
    x >>= 32;
    hi = (Uint32) (x & 0xFFFFFFFF);
    x = swap32(lo);
    x <<= 32;
    x |= swap32(hi);
    return x;
}
#else
#define swap32(x) (x)
#define swap64(x) (x)
#endif

#define MAX_CALLSTACKS 32

typedef enum
{
    ALEE_EOS,
    ALEE_ALERROR_TRIGGERED,
    ALEE_ALCERROR_TRIGGERED,
    ALEE_NEW_CALLSTACK_SYMS,
    ALEE_DEVICE_STATE_CHANGED_BOOL,
    ALEE_DEVICE_STATE_CHANGED_INT,
    ALEE_CONTEXT_STATE_CHANGED_ENUM,
    ALEE_CONTEXT_STATE_CHANGED_FLOAT,
    ALEE_CONTEXT_STATE_CHANGED_STRING,
    ALEE_LISTENER_STATE_CHANGED_FLOATV,
    ALEE_SOURCE_STATE_CHANGED_BOOL,
    ALEE_SOURCE_STATE_CHANGED_ENUM,
    ALEE_SOURCE_STATE_CHANGED_INT,
    ALEE_SOURCE_STATE_CHANGED_UINT,
    ALEE_SOURCE_STATE_CHANGED_FLOAT,
    ALEE_SOURCE_STATE_CHANGED_FLOAT3,
    ALEE_BUFFER_STATE_CHANGED_INT,
    #define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) ALEE_##name,
    #include "altrace_entrypoints.h"
    ALEE_MAX
} EventEnum;


#define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) extern ret (*REAL_##name) params;
#include "altrace_entrypoints.h"

void *get_ioblob(const size_t len);
void free_ioblobs(void);
__attribute__((noreturn)) void out_of_memory(void);
char *sprintf_alloc(const char *fmt, ...);
uint32 now(void);
int init_clock(void);
int load_real_openal(void);
void close_real_openal(void);

typedef struct StringCache StringCache;
const char *stringcache(StringCache *cache, const char *str);
StringCache *stringcache_create(void);
void stringcache_destroy(StringCache *cache);

#ifdef __cplusplus
}
#endif

#define MAP_DECL(maptype, fromctype, toctype) \
    void add_##maptype##_to_map(fromctype from, toctype to); \
    toctype get_mapped_##maptype(fromctype from); \
    void free_##maptype##_map(void)

#define SIMPLE_MAP(maptype, fromctype, toctype) \
    typedef struct SimpleMap_##maptype { \
        fromctype from; \
        toctype to; \
    } SimpleMap_##maptype; \
    static SimpleMap_##maptype *simplemap_##maptype = NULL; \
    static uint32 simplemap_##maptype##_map_size = 0; \
    void add_##maptype##_to_map(fromctype from, toctype to) { \
        void *ptr; uint32 i; \
        for (i = 0; i < simplemap_##maptype##_map_size; i++) { \
            if (simplemap_##maptype[i].from == from) { \
                free_hash_item_##maptype(simplemap_##maptype[i].from, simplemap_##maptype[i].to); \
                simplemap_##maptype[i].from = from; \
                simplemap_##maptype[i].to = to; \
                return; \
            } \
        } \
        ptr = realloc(simplemap_##maptype, (simplemap_##maptype##_map_size + 1) * sizeof (SimpleMap_##maptype)); \
        if (!ptr) { \
            out_of_memory(); \
        } \
        simplemap_##maptype = (SimpleMap_##maptype *) ptr; \
        simplemap_##maptype[simplemap_##maptype##_map_size].from = from; \
        simplemap_##maptype[simplemap_##maptype##_map_size].to = to; \
        simplemap_##maptype##_map_size++; \
    } \
    toctype get_mapped_##maptype(fromctype from) { \
        uint32 i; \
        for (i = 0; i < simplemap_##maptype##_map_size; i++) { \
            if (simplemap_##maptype[i].from == from) { \
                return simplemap_##maptype[i].to; \
            } \
        } \
        return (toctype) 0; \
    } \
    void free_##maptype##_map(void) { \
        uint32 i; \
        for (i = 0; i < simplemap_##maptype##_map_size; i++) { \
            SimpleMap_##maptype *item = &simplemap_##maptype[i]; \
            free_hash_item_##maptype(item->from, item->to); \
        } \
        free(simplemap_##maptype); \
        simplemap_##maptype = NULL; \
        simplemap_##maptype##_map_size = 0; \
    }


#define HASH_MAP(maptype, fromctype, toctype) \
    typedef struct HashMap_##maptype { \
        fromctype from; \
        toctype to; \
        struct HashMap_##maptype *next; \
    } HashMap_##maptype; \
    static HashMap_##maptype *hashmap_##maptype[256]; \
    static HashMap_##maptype *get_hashitem_##maptype(fromctype from, uint8 *_hash) { \
        const uint8 hash = hash_##maptype(from); \
        HashMap_##maptype *prev = NULL; \
        HashMap_##maptype *item = hashmap_##maptype[hash]; \
        if (_hash) { *_hash = hash; } \
        while (item) { \
            if (item->from == from) { \
                if (prev) { /* move to front of list */ \
                    prev->next = item->next; \
                    item->next = hashmap_##maptype[hash]; \
                    hashmap_##maptype[hash] = item; \
                } \
                return item; \
            } \
            prev = item; \
            item = item->next; \
        } \
        return NULL; \
    } \
    void add_##maptype##_to_map(fromctype from, toctype to) { \
        uint8 hash; HashMap_##maptype *item = get_hashitem_##maptype(from, &hash); \
        if (item) { \
            free_hash_item_##maptype(item->from, item->to); \
            item->from = from; \
            item->to = to; \
        } else { \
            item = (HashMap_##maptype *) calloc(1, sizeof (HashMap_##maptype)); \
            if (!item) { \
                out_of_memory(); \
            } \
            item->from = from; \
            item->to = to; \
            item->next = hashmap_##maptype[hash]; \
            hashmap_##maptype[hash] = item; \
        } \
    } \
    toctype get_mapped_##maptype(fromctype from) { \
        HashMap_##maptype *item = get_hashitem_##maptype(from, NULL); \
        return item ? item->to : (toctype) 0; \
    } \
    void free_##maptype##_map(void) { \
        int i; \
        for (i = 0; i < 256; i++) { \
            HashMap_##maptype *item; HashMap_##maptype *next; \
            for (item = hashmap_##maptype[i]; item; item = next) { \
                free_hash_item_##maptype(item->from, item->to); \
                next = item->next; \
                free(item); \
            } \
            hashmap_##maptype[i] = NULL; \
        } \
    }

#endif

// end of altrace_common.h ...

