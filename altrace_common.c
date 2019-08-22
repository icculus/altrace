/**
 * alTrace; a debugging tool for OpenAL.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "altrace_common.h"

#define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) ret (*REAL_##name) params = NULL;
#include "altrace_entrypoints.h"


#define MAX_IOBLOBS 256
static uint8 *ioblobs[MAX_IOBLOBS];
static size_t ioblobs_len[MAX_IOBLOBS];
static int next_ioblob = 0;

void *get_ioblob(const size_t len)
{
    void *ptr = ioblobs[next_ioblob];
    if (len > ioblobs_len[next_ioblob]) {
        //printf("allocating ioblob of %llu bytes...\n", (unsigned long long) len);
        ptr = realloc(ptr, len);
        if (!ptr) {
            out_of_memory();
        }
        ioblobs[next_ioblob] = (uint8 *) ptr;
        ioblobs_len[next_ioblob] = len;
    }
    next_ioblob++;
    if (next_ioblob >= ((sizeof (ioblobs) / sizeof (ioblobs[0])))) {
        next_ioblob = 0;
    }
    return ptr;
}

void free_ioblobs(void)
{
    size_t i;
    for (i = 0; i < (sizeof (ioblobs) / sizeof (ioblobs[0])); i++) {
        free(ioblobs[i]);
        ioblobs[i] = NULL;
        ioblobs_len[i] = 0;
    }
    next_ioblob = 0;
}

char *sprintf_alloc(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const size_t len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    char *retval = (char *) get_ioblob(len + 1);
    va_start(ap, fmt);
    if (vsnprintf(retval, len + 1, fmt, ap) != len) {
        retval = NULL;
    }
    va_end(ap);

    return retval;
}

static struct timespec starttime;
uint32 now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1) {
        fprintf(stderr, "%s: Failed to get current clock time: %s\n", GAppName, strerror(errno));
        return 0;
    }

    return (uint32)
        ( ( (((uint64) ts.tv_sec) * 1000) + (((uint64) ts.tv_nsec) / 1000000) ) -
          ( (((uint64) starttime.tv_sec) * 1000) + (((uint64) starttime.tv_nsec) / 1000000) ) );
}

int init_clock(void)
{
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &starttime) == -1) {
        fprintf(stderr, "%s: Failed to get current clock time: %s\n", GAppName, strerror(errno));
        return 0;
    }
    usleep(1000);  // just so now() is (hopefully) never 0
    return 1;
}

static void *realdll = NULL;

// !!! FIXME: we should use al[c]GetProcAddress() and do it _per device_ and
// !!! FIXME:  _per context_.
static void *loadEntryPoint(void *dll, const char *fnname, const int extension, int *okay)
{
    void *fn = dlsym(dll, fnname);
    if (!fn && !extension) {
        fprintf(stderr, "%s: Real OpenAL library doesn't have entry point '%s'!\n", GAppName, fnname);
        *okay = 0;
    }
    return fn;
}

int load_real_openal(void)
{
    int extensions = 0;
    int okay = 1;
    #ifdef __APPLE__
    const char *dllname = "libopenal.1.dylib";
    #elif defined(_WIN32)
    const char *dllname = "openal32.dll";
    #else
    const char *dllname = "libopenal.so.1";
    #endif

    realdll = dlopen(dllname, RTLD_NOW | RTLD_LOCAL);
    if (!realdll) {
        fprintf(stderr, "%s: Failed to load %s: %s\n", GAppName, dllname, dlerror());
        fflush(stderr);
    }

    if (!realdll) {
        // Not in the libpath? See if we can find it in the cwd.
        char *cwd = getcwd(NULL, 0);
        if (cwd) {
            const size_t fulllen = strlen(cwd) + strlen(dllname) + 2;
            char *fullpath = (char *) malloc(fulllen);
            if (fullpath) {
                snprintf(fullpath, fulllen, "%s/%s", cwd, dllname);
                realdll = dlopen(dllname, RTLD_NOW | RTLD_LOCAL);
                if (!realdll) {
                    fprintf(stderr, "%s: Failed to load %s: %s\n", GAppName, fullpath, dlerror());
                    fflush(stderr);
                }
                free(fullpath);
            }
            free(cwd);
        }
    }

    if (!realdll) {
        fprintf(stderr, "%s: Couldn't load OpenAL from anywhere obvious. Giving up.\n", GAppName);
        fflush(stderr);
        return 0;
    }

    #define ENTRYPOINT_EXTENSIONS_BEGIN() extensions = 1;
    #define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) REAL_##name = (ret (*)params) loadEntryPoint(realdll, #name, extensions, &okay);
    #include "altrace_entrypoints.h"
    return okay;
}

void close_real_openal(void)
{
    void *dll = realdll;

    realdll = NULL;

    #define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) REAL_##name = NULL;
    #include "altrace_entrypoints.h"

    if (dll) {
        dlclose(dll);
    }
}


// stole this from MojoShader: https://icculus.org/mojoshader
//  (I wrote this code, and it's zlib-licensed even if I didn't.  --ryan.)
typedef struct StringBucket
{
    char *string;
    struct StringBucket *next;
} StringBucket;

struct StringCache
{
    StringBucket **hashtable;
    uint32 table_size;
    void *d;
};

static inline uint32 hash_string(const char *str, size_t len)
{
    register uint32 hash = 5381;
    while (len--)
        hash = ((hash << 5) + hash) ^ *(str++);
    return hash;
} // hash_string

static const char *stringcache_len_internal(StringCache *cache,
                                            const char *str,
                                            const unsigned int len,
                                            const int addmissing)
{
    const uint8 hash = hash_string(str, len) & (cache->table_size-1);
    StringBucket *bucket = cache->hashtable[hash];
    StringBucket *prev = NULL;
    while (bucket)
    {
        const char *bstr = bucket->string;
        if ((strncmp(bstr, str, len) == 0) && (bstr[len] == 0))
        {
            // Matched! Move this to the front of the list.
            if (prev != NULL)
            {
                assert(prev->next == bucket);
                prev->next = bucket->next;
                bucket->next = cache->hashtable[hash];
                cache->hashtable[hash] = bucket;
            } // if
            return bstr; // already cached
        } // if
        prev = bucket;
        bucket = bucket->next;
    } // while

    // no match!
    if (!addmissing)
        return NULL;

    // add to the table.
    bucket = (StringBucket *) malloc(sizeof (StringBucket) + len + 1);
    if (bucket == NULL)
        return NULL;
    bucket->string = (char *)(bucket + 1);
    memcpy(bucket->string, str, len);
    bucket->string[len] = '\0';
    bucket->next = cache->hashtable[hash];
    cache->hashtable[hash] = bucket;
    return bucket->string;
} // stringcache_len_internal

static const char *stringcache_len(StringCache *cache, const char *str, const unsigned int len)
{
    return stringcache_len_internal(cache, str, len, 1);
} // stringcache_len

const char *stringcache(StringCache *cache, const char *str)
{
    return stringcache_len(cache, str, strlen(str));
} // stringcache

StringCache *stringcache_create(void)
{
    const uint32 initial_table_size = 256;
    const size_t tablelen = sizeof (StringBucket *) * initial_table_size;
    StringCache *cache = (StringCache *) malloc(sizeof (StringCache));
    if (!cache) {
        return NULL;
    }
    memset(cache, '\0', sizeof (StringCache));

    cache->hashtable = (StringBucket **) malloc(tablelen);
    if (!cache->hashtable)
    {
        free(cache);
        return NULL;
    }
    memset(cache->hashtable, '\0', tablelen);

    cache->table_size = initial_table_size;
    return cache;
} // stringcache_create

void stringcache_destroy(StringCache *cache)
{
    size_t i;

    if (cache == NULL)
        return;

    for (i = 0; i < cache->table_size; i++)
    {
        StringBucket *bucket = cache->hashtable[i];
        cache->hashtable[i] = NULL;
        while (bucket)
        {
            StringBucket *next = bucket->next;
            free(bucket);
            bucket = next;
        } // while
    } // for

    free(cache->hashtable);
    free(cache);
} // stringcache_destroy

// end of altrace_common.c ...

