/**
 * alTrace; a debugging tool for OpenAL.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_ALTRACE_PLAYBACK_H_
#define _INCL_ALTRACE_PLAYBACK_H_

#include "altrace_common.h"

// We assume we can cast the 64-bit values in the tracefile into pointers.
// It's my (untested) belief that you can record on a 32-bit platform and
//  play it back on a 64-bit system, but not the other way around.
#ifndef __LP64__
#error This currently expects a 64-bit target. 32-bits unsupported.
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CallstackFrame
{
    void *frame;
    const char *sym;
} CallstackFrame;

typedef struct CallerInfo
{
    CallstackFrame callstack[MAX_CALLSTACKS];
    int num_callstack_frames;
    int numargs;
    uint32 threadid;
    uint32 trace_scope;
    uint32 wait_until;
    off_t fdoffset;
    void *userdata;
} CallerInfo;

MAP_DECL(device, ALCdevice *, ALCdevice *);
MAP_DECL(context, ALCcontext *, ALCcontext *);
MAP_DECL(devicelabel, ALCdevice *, char *);
MAP_DECL(contextlabel, ALCcontext *, char *);
MAP_DECL(source, ALuint, ALuint);
MAP_DECL(buffer, ALuint, ALuint);
MAP_DECL(sourcelabel, ALuint, char *);
MAP_DECL(bufferlabel, ALuint, char *);
MAP_DECL(stackframe, void *, char *);
MAP_DECL(threadid, uint64, uint32);

#define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) void visit_##name visitparams;
#include "altrace_entrypoints.h"

void visit_al_error_event(void *userdata, const ALenum err);
void visit_alc_error_event(void *userdata, ALCdevice *device, const ALCenum err);
void visit_device_state_changed_int(void *userdata, ALCdevice *dev, const ALCenum param, const ALCint newval);
void visit_context_state_changed_enum(void *userdata, ALCcontext *ctx, const ALenum param, const ALenum newval);
void visit_context_state_changed_float(void *userdata, ALCcontext *ctx, const ALenum param, const ALfloat newval);
void visit_context_state_changed_string(void *userdata, ALCcontext *ctx, const ALenum param, const ALchar *str);
void visit_listener_state_changed_floatv(void *userdata, ALCcontext *ctx, const ALenum param, const uint32 numfloats, const ALfloat *values);
void visit_source_state_changed_bool(void *userdata, const ALuint name, const ALenum param, const ALboolean newval);
void visit_source_state_changed_enum(void *userdata, const ALuint name, const ALenum param, const ALenum newval);
void visit_source_state_changed_int(void *userdata, const ALuint name, const ALenum param, const ALint newval);
void visit_source_state_changed_uint(void *userdata, const ALuint name, const ALenum param, const ALuint newval);
void visit_source_state_changed_float(void *userdata, const ALuint name, const ALenum param, const ALfloat newval);
void visit_source_state_changed_float3(void *userdata, const ALuint name, const ALenum param, const ALfloat newval1, const ALfloat newval2, const ALfloat newval3);
void visit_buffer_state_changed_int(void *userdata, const ALuint name, const ALenum param, const ALint newval);
void visit_eos(void *userdata, const ALboolean okay, const uint32 wait_until);
int visit_progress(void *userdata, const off_t current, const off_t total);

const char *alcboolString(const ALCboolean x);
const char *alboolString(const ALCboolean x);
const char *alcenumString(const ALCenum x);
const char *alenumString(const ALCenum x);
const char *litString(const char *str);
const char *ptrString(const void *ptr);
const char *ctxString(ALCcontext *ctx);
const char *deviceString(ALCdevice *device);
const char *sourceString(const ALuint name);
const char *bufferString(const ALuint name);

int process_tracelog(const char *filename, void *userdata);

#ifdef __cplusplus
}
#endif

#endif

// end of altrace_playback.h ...

