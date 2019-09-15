/**
 * alTrace; a debugging tool for OpenAL.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

// we do some nasty conversions between pointers and integers in the StateTrie,
//  and we assume we can cast the 64-bit values in the tracefile into pointers.
// It's my (untested) belief that you can record on a 32-bit platform and
//  play it back on a 64-bit system, but not the other way around.
#ifndef __LP64__
#error This currently expects a 64-bit target. 32-bits unsupported.
#endif

#include <float.h>
#include <math.h>

#include "altrace_playback.h"

#include <wx/wx.h>
#include <wx/config.h>
#include <wx/splitter.h>
#include <wx/grid.h>
#include <wx/dataview.h>
#include <wx/notebook.h>
#include <wx/html/htmlwin.h>
#include <wx/msgdlg.h>
#include <wx/aboutdlg.h>
#include <wx/progdlg.h>
#include <wx/tokenzr.h>
#include <wx/html/m_templ.h>

#include "messageboxex.h"
#include "phamt.h"

static bool openal_loaded = false;
const char *GAppName = "altrace_wx";

static StringCache *appstringcache = NULL;

static const char *cache_string(const char *str)
{
    return str ? stringcache(appstringcache, str) : NULL;
}


static const wxString oom_msg(wxT("Out of memory!"));
static const wxString oom_title(wxT("Fatal error!"));
void out_of_memory(void)
{
    fputs(GAppName, stderr);
    fputs(": Out of memory!\n", stderr);
    fflush(stderr);
    wxMessageBox(oom_msg, oom_title);  // this might fail, oh well.
    _exit(42);
}


// this is djb's xor hashing function.
static inline uint32 hashCStringXorDJB(const char *str)
{
    size_t len = strlen(str);
    register uint32 hash = 5381;
    while (len--)
        hash = ((hash << 5) + hash) ^ *(str++);
    return hash;
}

template<> uint32 hashCalculate(const char * const &str)
{
    return hashCStringXorDJB(str);
}

template<> bool hashFromMatch(const char * const &a, const char * const &b)
{
    return (strcmp(a, b) == 0);
}


enum ApiArgType
{
    ARG_device,
    ARG_context,
    ARG_source,
    ARG_buffer,
    ARG_ptr,
    ARG_sizei,
    ARG_string,
    ARG_alint,
    ARG_aluint,
    ARG_alfloat,
    ARG_alcenum,
    ARG_alenum,
    ARG_aldouble,
    ARG_alcbool,
    ARG_albool
};

struct ApiArgInfo
{
    const char *name;
    ApiArgType type;
    union
    {
        ALCdevice *device;
        ALCcontext *context;
        ALuint source;
        ALuint buffer;
        const void *ptr;
        ALsizei sizei;
        const char *string;
        ALint alint;
        ALuint aluint;
        ALfloat alfloat;
        ALCenum alcenum;
        ALenum alenum;
        ALdouble aldouble;
        ALCboolean alcbool;
        ALboolean albool;
    };
};


class StateTrie : PersistentTrie<const char *, uint64>
{
public:
    StateTrie *snapshotState() {
        return (StateTrie *) snapshot();  // careful, this only works because StateTrie adds no data or vtable!
    }

    ALCcontext *getCurrentContext(ALCdevice **_device=NULL) const {
        const uint64 *val = getGlobalState("current_context");
        const uint64 current = val ? *val : 0;
        ALCcontext *ctx = (ALCcontext *) current;
        if (_device) {
            val = getContextState(ctx, "device");
            *_device = (ALCdevice *) (val ? *val : 0);
        }
        return ctx;
    }

    void setCurrentContext(ALCcontext *ctx) {
        addGlobalStateRevision("current_context", (uint64) ctx);
    }

    void addSourceStateRevision(ALCcontext *ctx, const ALuint name, const char *type, const uint64 newval) {
        char buf[64];
        snprintf(buf, sizeof (buf), "source://%p/%u/%s", ctx, (uint) name, type);
        addStateRevision(buf, newval);
    }

    void addBufferStateRevision(ALCdevice *device, const ALuint name, const char *type, const uint64 newval) {
        char buf[64];
        snprintf(buf, sizeof (buf), "buffer://%p/%u/%s", device, (uint) name, type);
        addStateRevision(buf, newval);
    }

    void addDeviceStateRevision(ALCdevice *device, const char *type, const uint64 newval) {
        char buf[64];
        snprintf(buf, sizeof (buf), "device://%p/%s", device, type);
        addStateRevision(buf, newval);
    }

    void addContextStateRevision(ALCcontext *context, const char *type, const uint64 newval) {
        char buf[64];
        snprintf(buf, sizeof (buf), "context://%p/%s", context, type);
        addStateRevision(buf, newval);
    }

    void addGlobalStateRevision(const char *type, const uint64 newval) {
        char buf[64];
        snprintf(buf, sizeof (buf), "global://%s", type);
        addStateRevision(buf, newval);
    }

    const uint64 *getSourceState(ALCcontext *context, const ALuint name, const char *type) const {
        char buf[64];
        snprintf(buf, sizeof (buf), "source://%p/%u/%s", context, (uint) name, type);
        return getState(buf);
    }

    const uint64 *getBufferState(ALCdevice *device, const ALuint name, const char *type) const {
        char buf[64];
        snprintf(buf, sizeof (buf), "buffer://%p/%u/%s", device, (uint) name, type);
        return getState(buf);
    }

    const uint64 *getDeviceState(ALCdevice *device, const char *type) const {
        char buf[64];
        snprintf(buf, sizeof (buf), "device://%p/%s", device, type);
        return getState(buf);
    }

    const uint64 *getContextState(ALCcontext *context, const char *type) const {
        char buf[64];
        snprintf(buf, sizeof (buf), "context://%p/%s", context, type);
        return getState(buf);
    }

    const uint64 *getGlobalState(const char *type) const {
        char buf[64];
        snprintf(buf, sizeof (buf), "global://%s", type);
        return getState(buf);
    }

private:
    const uint64 *getState(const char *key) const {
        return get(key);
    }

    void addStateRevision(const char *key, const uint64 newval) {
        const uint64 *val = get(key);
        if (val && (*val == newval)) {
            return;  // already set to this.
        }
        //printf("Putting '%s' [0x%X] => '%llu'\n", key, hashCalculate(key), (unsigned long long) newval);
        // cache_string() so we only keep one unique copy of the key and the trie doesn't have to free the memory.
        put(cache_string(key), newval);
    }
};


struct ApiCallInfo
{
    ApiCallInfo(const char *_fnname, const EventEnum _ev, const int _numargs, const CallerInfo *callerinfo)
        : fnname(_fnname)
        , callstr(NULL)
        , ev(_ev)
        , numargs(_numargs)
        , arginfo(_numargs ? new ApiArgInfo[_numargs] : NULL)
        , retinfo(NULL)
        , numretinfo(0)
        , trace_scope(callerinfo->trace_scope)
        , num_callstack_frames(callerinfo->num_callstack_frames)
        , callstack(new CallstackFrame[num_callstack_frames])
        , threadid(callerinfo->threadid)
        , timestamp(callerinfo->wait_until)
        , state(NULL)
        , generated_al_error(AL_FALSE)
        , generated_alc_error(AL_FALSE)
        , reported_failure(AL_FALSE)
        , inefficient_state_change(AL_FALSE)
    {
        CallstackFrame *stack = const_cast<CallstackFrame *>(callstack);
        memcpy(stack, callerinfo->callstack, num_callstack_frames * sizeof (CallstackFrame));
        for (int i = 0; i < num_callstack_frames; i++) {
            stack[i].sym = cache_string(stack[i].sym);
        }
    }

    ~ApiCallInfo()
    {
        delete[] const_cast<CallstackFrame *>(callstack);
        delete[] const_cast<ApiArgInfo *>(arginfo);
        if (numretinfo == 0) {
            delete retinfo;
        } else {
            delete[] retinfo;
        }
        delete state;
    }

    const char *fnname;
    const char *callstr;
    const EventEnum ev;
    const int numargs;
    ApiArgInfo *arginfo;
    ApiArgInfo *retinfo;
    int numretinfo;
    const uint32 trace_scope;
    const int num_callstack_frames;
    const CallstackFrame *callstack;
    const uint32 threadid;
    const uint32 timestamp;
    StateTrie *state;
    ALboolean generated_al_error;
    ALboolean generated_alc_error;
    ALboolean reported_failure;
    ALboolean inefficient_state_change;
};


class ALTraceAudioPlayerCtrl : public wxControl
{
public:
    ALTraceAudioPlayerCtrl(wxWindow *parent, wxWindowID id, const wxPoint &pos=wxDefaultPosition, const wxSize &size=wxDefaultSize, long style=0, const wxValidator &validator=wxDefaultValidator, const wxString &name=wxControlNameStr);
    virtual ~ALTraceAudioPlayerCtrl();
    void setAudio(const float *pcm, const size_t pcmbytes, const unsigned int numchannels, const unsigned int freq);
    void setAudio(const int16 *pcm, const size_t pcmbytes, const unsigned int numchannels, const unsigned int freq);
    void setAudio(const uint8 *pcm, const size_t pcmbytes, const unsigned int numchannels, const unsigned int freq);
    void clearAudio();
    void play();
    void stop();

    // wxWidgets event handlers...
    void onResize(wxSizeEvent &event);
    void onErase(wxEraseEvent &event);
    void onPaint(wxPaintEvent &event);
    void onIdle(wxIdleEvent &event);
    void onMouseLeftDown(wxMouseEvent &event);

private:
    int16 *pcm;
    size_t pcmsamples;
    size_t pcmfreq;
    unsigned int pcmchannels;
    size_t pcmposition;
    size_t lastdrawpos;
    wxBitmap *backing;

    ALCdevice *device;
    ALCcontext *context;
    ALuint sid;
    ALuint bid;

    void updateBackingWaveform();
    bool preparePlayback();
    void shutdownPlayback();

    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ALTraceAudioPlayerCtrl, wxControl)
    EVT_SIZE(ALTraceAudioPlayerCtrl::onResize)
    EVT_PAINT(ALTraceAudioPlayerCtrl::onPaint)
    EVT_ERASE_BACKGROUND(ALTraceAudioPlayerCtrl::onErase)
    EVT_LEFT_DOWN(ALTraceAudioPlayerCtrl::onMouseLeftDown)
END_EVENT_TABLE()


ALTraceAudioPlayerCtrl::ALTraceAudioPlayerCtrl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxValidator &validator, const wxString &name)
    : wxControl(parent, id, pos, size, style, validator, name)
    , pcm(NULL)
    , pcmsamples(0)
    , pcmfreq(0)
    , pcmchannels(0)
    , pcmposition(0)
    , lastdrawpos(0xFFFFFFFF)
    , backing(NULL)
    , device(NULL)
    , context(NULL)
    , sid(0)
    , bid(0)
{
}

ALTraceAudioPlayerCtrl::~ALTraceAudioPlayerCtrl()
{
    shutdownPlayback();
    delete[] pcm;
    delete backing;
}

void ALTraceAudioPlayerCtrl::shutdownPlayback()
{
    Disconnect(wxEVT_IDLE, wxIdleEventHandler(ALTraceAudioPlayerCtrl::onIdle));
    lastdrawpos = 0xFFFFFFFF;
    Refresh();

    if (sid) {
        REAL_alSourceStop(sid);
        REAL_alSourcei(sid, AL_BUFFER, 0);
        REAL_alDeleteSources(1, &sid);
        sid = 0;
    }

    if (bid) {
        REAL_alDeleteBuffers(1, &bid);
        bid = 0;
    }

    if (context) {
        REAL_alcMakeContextCurrent(NULL);
        REAL_alcDestroyContext(context);
        context = NULL;
    }

    if (device) {
        REAL_alcCloseDevice(device);
        device = NULL;
    }
}

bool ALTraceAudioPlayerCtrl::preparePlayback()
{
    if (!openal_loaded || !pcm) {
        return false;
    }

    if (!device) {
        device = REAL_alcOpenDevice(NULL);
        if (!device) {
            wxMessageBox(wxT("Couldn't open OpenAL device, playback disabled."), wxT("ERROR"));
            return false;
        }

        static bool reported = false;
        if (!reported) {
            printf("ALC_DEVICE_SPECIFIER: %s\n", (const char *) REAL_alcGetString(device, ALC_DEVICE_SPECIFIER));
            printf("ALC_EXTENSIONS: %s\n", (const char *) REAL_alcGetString(device, ALC_EXTENSIONS));
            reported = true;
        }
    }

    if (!context) {
        context = REAL_alcCreateContext(device, NULL);
        if (!context) {
            wxMessageBox(wxT("Couldn't create OpenAL context, playback disabled."), wxT("ERROR"));
            shutdownPlayback();
            return false;
        }

        if (!REAL_alcMakeContextCurrent(context)) {
            wxMessageBox(wxT("Couldn't make OpenAL context current, playback disabled."), wxT("ERROR"));
            shutdownPlayback();
            return false;
        }

        static bool reported = false;
        if (!reported) {
            printf("AL_RENDERER: %s\n", (const char *) REAL_alGetString(AL_RENDERER));
            printf("AL_VERSION: %s\n", (const char *) REAL_alGetString(AL_VERSION));
            printf("AL_VENDOR: %s\n", (const char *) REAL_alGetString(AL_VENDOR));
            printf("AL_EXTENSIONS: %s\n", (const char *) REAL_alGetString(AL_EXTENSIONS));
            reported = true;
        }
    }

    if (!sid) {
        REAL_alGenSources(1, &sid);
        if (!sid) {
            wxMessageBox(wxT("Couldn't generate OpenAL source, playback disabled."), wxT("ERROR"));
            shutdownPlayback();
            return false;
        }
    }

    if (!bid) {
        REAL_alGenBuffers(1, &bid);
        if (!bid) {
            wxMessageBox(wxT("Couldn't generate OpenAL buffer, playback disabled."), wxT("ERROR"));
            shutdownPlayback();
            return false;
        }
    }

    REAL_alGetError();
    // !!! FIXME: > stereo channels?
    REAL_alBufferData(bid, (pcmchannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, pcm, pcmsamples * 2, pcmfreq);
    if (REAL_alGetError() != AL_NO_ERROR) {
        wxMessageBox(wxT("Couldn't upload audio to OpenAL buffer, playback disabled."), wxT("ERROR"));
        shutdownPlayback();
        return false;
    }

    REAL_alSourcei(sid, AL_BUFFER, bid);
    if (REAL_alGetError() != AL_NO_ERROR) {
        wxMessageBox(wxT("Couldn't assign OpenAL buffer to source, playback disabled."), wxT("ERROR"));
        shutdownPlayback();
        return false;
    }

    return true;
}

void ALTraceAudioPlayerCtrl::play()
{
    if (!backing) return;  // we...can't draw...
    if (!preparePlayback()) return;
    lastdrawpos = 0xFFFFFFFF;
    REAL_alSourcePlay(sid);
    Connect(wxID_ANY, wxEVT_IDLE, wxIdleEventHandler(ALTraceAudioPlayerCtrl::onIdle));
}

void ALTraceAudioPlayerCtrl::stop()
{
    shutdownPlayback();
}

void ALTraceAudioPlayerCtrl::updateBackingWaveform()
{
    if (!backing) {
        return;  // will try again when resized for the first time.
    }

    wxMemoryDC dc(*backing);

    const int w = backing->GetWidth();
    const int h = backing->GetHeight();
    const int halfh = h / 2;
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();
    dc.SetPen(*wxWHITE_PEN);
    dc.DrawLine(0, halfh, w, halfh);

    if (!pcm || (h < 2)) {
        Refresh();
        Update();
        return;  // no waveform to draw.
    }

    const size_t frames = pcmsamples / pcmchannels;
    const float hf = (float) h;
    const float halfhf = hf * 0.5f;

    dc.SetPen(*wxGREEN_PEN);
    const size_t fpp = frames / w;  // frame per pixel.
    const size_t spp = fpp * pcmchannels; // samples per pixel.
    const int16 *ptr = pcm;
    int prevx = 0;
    int prevy = halfh;
    for (int i = 0; i < w; i++) {
        float power = 0.0f;
        for (int j = 0; j < spp; j++) {
            power += ((float) *(ptr++)) / 32767.0f;
        }
        power /= (float) spp;

        const int x = prevx + 1;
        const float fy = halfhf - (halfhf * power);
        const int y = (int) (fy + 0.5f);
        dc.DrawLine(prevx, prevy, x, y);
        prevx = x;
        prevy = y;
    }

    Refresh();
    Update();
}

void ALTraceAudioPlayerCtrl::onIdle(wxIdleEvent &event)
{
    const int w = backing->GetWidth();
    const int h = backing->GetHeight();
    const size_t prevdrawpos = lastdrawpos;

    ALint state = AL_STOPPED;
    if (sid && backing) {
        REAL_alGetSourcei(sid, AL_SOURCE_STATE, &state);
    }

    if (state != AL_PLAYING) {
        lastdrawpos = 0xFFFFFFFF;  // just remove the line.
        shutdownPlayback();
    } else {
        event.RequestMore();  // keep idle events coming.

        ALint samplepos = 0;
        REAL_alGetSourcei(sid, AL_SAMPLE_OFFSET, &samplepos);

        const size_t frames = pcmsamples / pcmchannels;
        const size_t fpp = w ? (frames / w) : 0;  // frame per pixel.
        const size_t spp = fpp * pcmchannels; // samples per pixel.

        const size_t newdrawpos = spp ? (((size_t) samplepos) / spp) : 0;
        if (newdrawpos == lastdrawpos) {
            return;  // nothing new yet!
        }
        lastdrawpos = newdrawpos;
    }

    // !!! FIXME: just overwrite the piece that is changing.
    Refresh();
    Update();
}

// !!! FIXME: this is a hack for now.
void ALTraceAudioPlayerCtrl::onMouseLeftDown(wxMouseEvent &event)
{
    if (sid) {
        stop();
    } else {
        play();
    }
}

void ALTraceAudioPlayerCtrl::onResize(wxSizeEvent &event)
{
    int w, h;
    GetClientSize(&w, &h);
    delete backing;
    backing = NULL;
    lastdrawpos = 0xFFFFFFFF;  // let onIdle sort it out.
    if ((w > 0) && (h > 0)) {
        backing = new wxBitmap(w, h);
        updateBackingWaveform();
    }
    event.Skip();
}

void ALTraceAudioPlayerCtrl::onErase(wxEraseEvent &event)
{
    // We don't want to do anything here, since we redraw the whole window
    //  in the Paint event...catching the erase event and doing nothing
    //  prevents flicker on some platforms, though.
    (void) event.GetDC();
}

void ALTraceAudioPlayerCtrl::onPaint(wxPaintEvent &event)
{
    int w, h;
    GetClientSize(&w, &h);

    wxPaintDC dc(this);
    if (backing) {
        dc.DrawBitmap(*backing, 0, 0);
        if (lastdrawpos < w) {
            dc.SetPen(*wxYELLOW_PEN);
            dc.DrawLine(lastdrawpos, 0, lastdrawpos, h);
        }
    } else {
        // no backing store?!? Just fake it.
    	dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
        dc.SetPen(*wxWHITE_PEN);
        dc.DrawLine(0, h / 2, w, h / 2);
    }
}

void ALTraceAudioPlayerCtrl::setAudio(const float *_pcm, const size_t pcmbytes, const unsigned int numchannels, const unsigned int freq)
{
    shutdownPlayback();
    pcmsamples = pcmbytes / sizeof (*_pcm);
    pcm = new int16[pcmsamples];
    pcmposition = 0;
    pcmchannels = numchannels;
    pcmfreq = freq;
    for (size_t i = 0; i < pcmsamples; i++) {
        pcm[i] = (int16) (_pcm[i] * 32767.0f);
    }
    updateBackingWaveform();
}

void ALTraceAudioPlayerCtrl::setAudio(const int16 *_pcm, const size_t pcmbytes, const unsigned int numchannels, const unsigned int freq)
{
    shutdownPlayback();
    pcmsamples = pcmbytes / sizeof (*_pcm);
    pcm = new int16[pcmsamples];
    pcmposition = 0;
    pcmchannels = numchannels;
    pcmfreq = freq;
    memcpy(pcm, _pcm, pcmbytes);
    updateBackingWaveform();
}

void ALTraceAudioPlayerCtrl::setAudio(const uint8 *_pcm, const size_t pcmbytes, const unsigned int numchannels, const unsigned int freq)
{
    shutdownPlayback();
    pcmsamples = pcmbytes / sizeof (*_pcm);
    pcm = new int16[pcmsamples];
    pcmposition = 0;
    pcmchannels = numchannels;
    pcmfreq = freq;
    for (size_t i = 0; i < pcmsamples; i++) {
        pcm[i] = (int16) (((((float) _pcm[i]) / 128.0f) - 1.0f) * 32767.0f);
    }
    updateBackingWaveform();
}

void ALTraceAudioPlayerCtrl::clearAudio()
{
    shutdownPlayback();
    delete[] pcm;
    pcm = NULL;
    pcmsamples = 0;
    pcmposition = 0;
    pcmchannels = 0;
    pcmfreq = 0;
    updateBackingWaveform();
}


class ALTraceFrame;

// This is a magic hack to add a "<uoff>" tag to the HTML parser, to remove
//  the underlink on hyperlinks.
// https://forums.wxwidgets.org/viewtopic.php?p=96724#p96724
TAG_HANDLER_BEGIN(FACES_UOFF, "UOFF")

    TAG_HANDLER_CONSTR(FACES_UOFF) { }

    TAG_HANDLER_PROC(tag)
    {
        const int underlined = m_WParser->GetFontUnderlined();

        m_WParser->SetFontUnderlined(false);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));

        ParseInner(tag);

        m_WParser->SetFontUnderlined(underlined);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        return true;
    }

TAG_HANDLER_END(FACES_UOFF)

TAGS_MODULE_BEGIN(FACES_UOFF)
	TAGS_MODULE_ADD(FACES_UOFF)
TAGS_MODULE_END(FACES_UOFF)


class ALTraceHtmlWindow : public wxHtmlWindow
{
public:
    ALTraceHtmlWindow(ALTraceFrame *_frame, wxWindow *parent, wxWindowID winid=wxID_ANY)
        : wxHtmlWindow(parent, winid)
        , frame(_frame)
    {
        resetPage();
    }

    wxString getHtmlForegroundColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT).GetAsString(wxC2S_HTML_SYNTAX); }
    wxString getHtmlBackgroundColor() const { return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW).GetAsString(wxC2S_HTML_SYNTAX); }

    void resetPage() {
        SetPage(wxString::Format(wxT("<html><body bgcolor='%s'></body></html>"), getHtmlBackgroundColor()));
    }

    void onLinkClicked(wxHtmlLinkEvent& event);

private:
    ALTraceFrame *frame;

    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ALTraceHtmlWindow, wxHtmlWindow)
    EVT_HTML_LINK_CLICKED(wxID_ANY, ALTraceHtmlWindow::onLinkClicked)
END_EVENT_TABLE()


class ALTraceListAndInfoPage : public wxSplitterWindow
{
public:
    ALTraceListAndInfoPage(ALTraceFrame *_frame, const wxString &listname, const wxString &cfgname, wxWindow *parent, wxWindowID winid=wxID_ANY);
    void updateItemList(const ApiCallInfo *info);
    bool selectItemByData(const wxUIntPtr data);
    void forceDetailsRedraw();

    void onResize(wxSizeEvent &event);
    void onSelectionChanged(wxDataViewEvent& event);
    void onSysColourChanged(wxSysColourChangedEvent& event);

protected:
    wxDataViewListCtrl *itemlist;
    ALTraceHtmlWindow *details;
    const ApiCallInfo *apiinfo;
    ALTraceFrame *frame;
    wxUIntPtr currentItemData;

    virtual void updateItemListImpl() = 0;
    virtual void updateDetails(const wxUIntPtr data) = 0;
    virtual void clearDetails() {
        details->resetPage();
    }

private:
    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ALTraceListAndInfoPage, wxSplitterWindow)
    EVT_SIZE(ALTraceListAndInfoPage::onResize)
    EVT_SYS_COLOUR_CHANGED(ALTraceListAndInfoPage::onSysColourChanged)
END_EVENT_TABLE()


ALTraceListAndInfoPage::ALTraceListAndInfoPage(ALTraceFrame *_frame, const wxString &listname, const wxString &cfgname, wxWindow *parent, wxWindowID winid)
    : wxSplitterWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE)
    , itemlist(NULL)
    , details(NULL)
    , apiinfo(NULL)
    , frame(_frame)
    , currentItemData(0)
{
    SetSashGravity(0.5);
    SetMinimumPaneSize(1);

    const int itemlistid = wxNewId();
    itemlist = new wxDataViewListCtrl(this, itemlistid, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_HORIZ_RULES);
    itemlist->AppendTextColumn(listname, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE);

    details = new ALTraceHtmlWindow(frame, this, wxID_ANY);
    clearDetails();

    SplitHorizontally(itemlist, details);

    long pos = 0;
    wxConfigBase *cfg = wxConfig::Get();
    if (cfg != NULL) {
        wxString cfgparam(cfgname);
        cfgparam << "SplitPos";
        if (cfg->Read(cfgparam, &pos)) {
            SetSashPosition((int) pos);
        }
    }

    Connect(itemlistid, wxEVT_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler(ALTraceListAndInfoPage::onSelectionChanged));
}

void ALTraceListAndInfoPage::onSysColourChanged(wxSysColourChangedEvent& event)
{
    event.Skip();  // pass on to other widgets.
    if (currentItemData) {
        updateDetails(currentItemData);
    } else {
        clearDetails();
    }
}

void ALTraceListAndInfoPage::onResize(wxSizeEvent &event)
{
    // make the list widget as wide as the page.
    const int w = GetClientSize().x;
    if (w > 8) {
        itemlist->GetColumn(0)->SetWidth(w - 8);
    }
    event.Skip();
}

void ALTraceListAndInfoPage::onSelectionChanged(wxDataViewEvent& event)
{
    if (event.GetEventObject() != itemlist) {  // just in case.
        event.Skip();
        return;
    }

    const int row = itemlist->GetSelectedRow();
    if (row == wxNOT_FOUND) {
        //clearDetails();
        return;
    }

    const wxUIntPtr data = itemlist->GetItemData(itemlist->RowToItem(row));
    currentItemData = data;
    updateDetails(data);
}


bool ALTraceListAndInfoPage::selectItemByData(const wxUIntPtr data)
{
    const unsigned int total = itemlist->GetItemCount();
    for (unsigned int row = 0; row < total; row++) {
     	const wxDataViewItem item = itemlist->RowToItem(row);
        if (itemlist->GetItemData(item) == data) {
            itemlist->SetCurrentItem(item);
            currentItemData = data;
            return true;
        }
    }

    return false;
}


void ALTraceListAndInfoPage::updateItemList(const ApiCallInfo *info)
{
    itemlist->DeleteAllItems();
    apiinfo = info;
    updateItemListImpl();

    const unsigned int numrows = itemlist->GetItemCount();
    if (!currentItemData || !numrows) {
        clearDetails();
    } else {  // try to reselect whatever was previously selected.
        bool found = false;
        for (unsigned int i = 0; i < numrows; i++) {
            const wxUIntPtr rowdata = itemlist->GetItemData(itemlist->RowToItem(i));
            if (currentItemData == rowdata) {
                found = true;
                itemlist->SelectRow(i);
                break;
            }
        }
    }
}

void ALTraceListAndInfoPage::forceDetailsRedraw()
{
    details->Refresh();
    details->Update();
}

class ALTraceCallInfoPage : public ALTraceHtmlWindow
{
public:
    ALTraceCallInfoPage(ALTraceFrame *_frame, wxWindow *parent, wxWindowID winid=wxID_ANY) : ALTraceHtmlWindow(_frame, parent, winid) {}
    void updateCallInfoPage(const ApiCallInfo *info);
};

class ALTraceDeviceInfoPage : public ALTraceListAndInfoPage
{
public:
    ALTraceDeviceInfoPage(ALTraceFrame *_frame, wxWindow *parent, wxWindowID winid=wxID_ANY) : ALTraceListAndInfoPage(_frame, wxT("Available devices"), wxT("DeviceInfo"), parent, winid) {}
protected:
    virtual void updateItemListImpl();
    virtual void updateDetails(const wxUIntPtr data);
};

class ALTraceContextInfoPage : public ALTraceListAndInfoPage
{
public:
    ALTraceContextInfoPage(ALTraceFrame *_frame, wxWindow *parent, wxWindowID winid=wxID_ANY) : ALTraceListAndInfoPage(_frame, wxT("Available contexts"), wxT("ContextInfo"), parent, winid) {}
protected:
    virtual void updateItemListImpl();
    virtual void updateDetails(const wxUIntPtr data);
};

class ALTraceSourceInfoPage : public ALTraceListAndInfoPage
{
public:
    ALTraceSourceInfoPage(ALTraceFrame *_frame, wxWindow *parent, wxWindowID winid=wxID_ANY) : ALTraceListAndInfoPage(_frame, wxT("Available sources"), wxT("SourceInfo"), parent, winid) {}
protected:
    virtual void updateItemListImpl();
    virtual void updateDetails(const wxUIntPtr data);
};

class ALTraceBufferInfoPage : public ALTraceListAndInfoPage
{
public:
    ALTraceBufferInfoPage(ALTraceFrame *_frame, wxWindow *parent, wxWindowID winid=wxID_ANY) : ALTraceListAndInfoPage(_frame, wxT("Available buffers"), wxT("BufferInfo"), parent, winid) {}
protected:
    virtual void updateItemListImpl();
    virtual void updateDetails(const wxUIntPtr data);
    virtual void clearDetails();
};


class ALTraceGrid;

class ALTraceGridTable : public wxGridTableBase
{
public:
    ALTraceGridTable();
    virtual ~ALTraceGridTable();

    void appendApiCall(ApiCallInfo *info, const CallerInfo *callerinfo);
    ApiCallInfo *getApiCallInfo(const int row=-1);

    // forwarded by ALTraceFrame, not an actual event handler.
    void onSysColourChanged(wxSysColourChangedEvent& event);

    uint32 getLatestCallTime() const { return latestCallTime; }
    uint32 getLargestThreadNum() const { return largestThreadNum; }

    virtual int GetNumberRows() { return numrows; }
    virtual int GetNumberCols() { return 3; }
    virtual bool IsEmptyCell(int row, int col) { return false; }
    virtual void SetValue(int row, int col, const wxString &value) { assert(!"Shouldn't call this"); }

    virtual bool CanGetValueAs(int row, int col, const wxString &typeName) {
        return typeName == ((col == 2) ? wxGRID_VALUE_STRING : wxGRID_VALUE_NUMBER);
    }

    virtual wxString GetTypeName(int row, int col) {
        return (col == 2) ? wxGRID_VALUE_STRING : wxGRID_VALUE_NUMBER;
    }

    virtual long GetValueAsLong(int row, int col) {
        assert(col >= 0);
        assert(col < 2);
        assert(row >= 0);
        assert(row < numrows);
        const ApiCallInfo *info = infoarray[row];
        if (col == 0) {
            return (long) info->threadid;
        } else if (col == 1) {
            return (long) info->timestamp;
        }
        return 0;
    }

    virtual wxString GetValue(int row, int col) {
        assert(col == 2);
        assert(row >= 0);
        assert(row < numrows);
        const ApiCallInfo *info = infoarray[row];
        return info->callstr;
    }

    virtual wxString GetColLabelValue(int col) {
        switch (col) {
            case 0: return wxT("thread");
            case 1: return wxT("time");
            case 2: return wxT("call");
            default: break;
        }
        return wxT("");
    }

    virtual bool CanHaveAttributes() { return true; }
    virtual wxGridCellAttr *GetAttr(int row, int col, wxGridCellAttr::wxAttrKind kind);

private:
    ApiCallInfo **infoarray;
    int numrows;
    uint32 latestCallTime;
    uint32 largestThreadNum;

    // !!! FIXME: don't name these with explicit colors.
    wxGridCellAttr *attrEvenRed;
    wxGridCellAttr *attrOddRed;
    wxGridCellAttr *attrEvenBlack;
    wxGridCellAttr *attrOddBlack;
    wxGridCellAttr *attrEvenDarkRed;
    wxGridCellAttr *attrOddDarkRed;

    void generateCellAttributes();
    void decrefCellAttributes();
};

ALTraceGridTable::ALTraceGridTable()
    : infoarray(NULL)
    , numrows(0)
    , latestCallTime(0)
    , largestThreadNum(0)
{
    generateCellAttributes();
}

ALTraceGridTable::~ALTraceGridTable()
{
    if (infoarray) {
        const int total = numrows;
        for (int i = 0; i < total; i++) {
            delete infoarray[i];
        }
        free(infoarray);
    }
    decrefCellAttributes();
}

void ALTraceGridTable::decrefCellAttributes()
{
    attrEvenRed->DecRef();
    attrOddRed->DecRef();
    attrEvenBlack->DecRef();
    attrOddBlack->DecRef();
    attrEvenDarkRed->DecRef();
    attrOddDarkRed->DecRef();
}

void ALTraceGridTable::generateCellAttributes()
{
    const wxColour darkred(0xAA, 0, 0);

    #ifdef __APPLE__  // deal with dark mode and real system colors.
    const wxColour textcolor(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    wxColour background_even, background_odd;
    extern void cocoaGetGridColors(wxColour *even, wxColor *odd);
    cocoaGetGridColors(&background_even, &background_odd);
    #else
    const wxColour background_even(255, 255, 255);
    const wxColour background_odd(244, 245, 245);
    const wxColour textcolor(*wxBLACK);
    #endif

    wxGridCellAttr *oddattr = new wxGridCellAttr;
    oddattr->SetAlignment(wxALIGN_LEFT, wxALIGN_CENTRE);
    oddattr->SetReadOnly();

    wxGridCellAttr *evenattr = oddattr->Clone();
    evenattr->SetBackgroundColour(background_even);
    oddattr->SetBackgroundColour(background_odd);

    // !!! FIXME: pick a different color than wxRED if the system background is too red.
    attrEvenRed = evenattr->Clone();
    attrEvenRed->SetTextColour(*wxRED);
    attrOddRed = oddattr->Clone();
    attrOddRed->SetTextColour(*wxRED);

    // !!! FIXME: pick a different color than darkred if the system background is too red.
    attrEvenDarkRed = evenattr->Clone();
    attrEvenDarkRed->SetTextColour(darkred);
    attrOddDarkRed = oddattr->Clone();
    attrOddDarkRed->SetTextColour(darkred);

    attrEvenBlack = evenattr->Clone();
    attrEvenBlack->SetTextColour(textcolor);
    attrOddBlack = oddattr->Clone();
    attrOddBlack->SetTextColour(textcolor);

    oddattr->DecRef();
    evenattr->DecRef();
}

wxGridCellAttr *ALTraceGridTable::GetAttr(int row, int col, wxGridCellAttr::wxAttrKind kind)
{
    #ifdef __APPLE__  // !!! FIXME: this needs to happen or changing to/from dark mode messes up.
    decrefCellAttributes();
    generateCellAttributes();
    #endif

    assert(row >= 0);
    assert(row < numrows);
    const ApiCallInfo *info = infoarray[row];
    wxGridCellAttr *attr = NULL;
    if (row & 0x1) {  // odd
        if (info->reported_failure)  {
            attr = attrOddRed;
        } else if (info->inefficient_state_change) {
            attr = attrOddDarkRed;
        } else {
            attr = attrOddBlack;
        }
    } else {  // even
        if (info->reported_failure)  {
            attr = attrEvenRed;
        } else if (info->inefficient_state_change) {
            attr = attrEvenDarkRed;
        } else {
            attr = attrEvenBlack;
        }
    }
    attr->IncRef();
    return attr;
}

void ALTraceGridTable::onSysColourChanged(wxSysColourChangedEvent& event)
{
    decrefCellAttributes();
    generateCellAttributes();
}

class ALTraceFrame : public wxFrame
{
public:
    ALTraceFrame();
    virtual ~ALTraceFrame();

    static const wxPoint getPreviousPos();
    static const wxSize getPreviousSize();

    bool openFile(const wxString &path);

    const wxString &getTracefilePath() const { return tracefile_path; }
    StateTrie *getStateTrie() { return &statetrie; }
    ALTraceGridTable *getApiCallGridTable() { return apiCallGridTable; }
    wxNotebook *getNotebook() { return stateNotebook; }
    ALTraceCallInfoPage *getCallInfoPage() { return callInfoPage; }
    ALTraceDeviceInfoPage *getDeviceInfoPage() { return deviceInfoPage; }
    ALTraceContextInfoPage *getContextInfoPage() { return contextInfoPage; }
    ALTraceSourceInfoPage *getSourceInfoPage() { return sourceInfoPage; }
    ALTraceBufferInfoPage *getBufferInfoPage() { return bufferInfoPage; }

    void setAudio(const uint64 playerid, const ALenum alfmt, const void *pcm, const size_t pcmbytes, const unsigned int freq);
    void clearAudio();
    uint64 getCurrentPlayerId() const { return current_player_id; }

    // wxWidgets event handlers...
    void onClose(wxCloseEvent &event);
    void onResize(wxSizeEvent &event);
    void onMove(wxMoveEvent &event);
    void onNotebookPageChanged(wxBookCtrlEvent& event);
    void onSysColourChanged(wxSysColourChangedEvent& event);
    void onMenuClose(wxCommandEvent& event);

private:
    wxSplitterWindow *topSplit;
    wxSplitterWindow *infoSplit;
    ALTraceGridTable *apiCallGridTable;
    ALTraceGrid *apiCallGrid;
    wxNotebook *stateNotebook;
    ALTraceCallInfoPage *callInfoPage;
    ALTraceDeviceInfoPage *deviceInfoPage;
    ALTraceSourceInfoPage *sourceInfoPage;
    ALTraceBufferInfoPage *bufferInfoPage;
    ALTraceContextInfoPage *contextInfoPage;
    ALTraceAudioPlayerCtrl *audioPlayer;
    uint64 current_player_id;
    wxString tracefile_path;

    int nonMaximizedX;
    int nonMaximizedY;
    int nonMaximizedWidth;
    int nonMaximizedHeight;

    StateTrie statetrie;

    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ALTraceFrame, wxFrame)
    EVT_CLOSE(ALTraceFrame::onClose)
    EVT_SIZE(ALTraceFrame::onResize)
    EVT_MOVE(ALTraceFrame::onMove)
    EVT_SYS_COLOUR_CHANGED(ALTraceFrame::onSysColourChanged)
    EVT_MENU(wxID_CLOSE, ALTraceFrame::onMenuClose)
END_EVENT_TABLE()

void ALTraceFrame::onMenuClose(wxCommandEvent& event)
{
printf("ON MENU CLOSE IN FRAME\n");
    Close(true);
}


class ALTraceGrid : public wxGrid
{
public:
    ALTraceGrid(ALTraceFrame *_frame, ALTraceGridTable *table, wxWindow *parent, wxWindowID winid=wxID_ANY);

    int getCurrentRow() const { return currentrow; }
    void setProcessing(const bool yesOrNo) { processing = yesOrNo; }

    // wxWidgets event handlers...
    void onResize(wxSizeEvent &event);
    void onRowChosen(wxGridEvent &event);
    void onMouseMotion(wxMouseEvent &event);

private:
    ALTraceFrame *frame;
    bool processing;
    int currentrow;

    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ALTraceGrid, wxGrid)
    EVT_SIZE(ALTraceGrid::onResize)
    EVT_GRID_CELL_LEFT_CLICK(ALTraceGrid::onRowChosen)
    EVT_GRID_LABEL_LEFT_CLICK(ALTraceGrid::onRowChosen)
    EVT_GRID_SELECT_CELL(ALTraceGrid::onRowChosen)
END_EVENT_TABLE()


ALTraceGrid::ALTraceGrid(ALTraceFrame *_frame, ALTraceGridTable *table, wxWindow *parent, wxWindowID winid)
    : wxGrid(parent, winid)
    , frame(_frame)
    , processing(false)
    , currentrow(-1)
{
    SetTable(table, false, wxGrid::wxGridSelectRows);
    DisableDragRowSize();
    SetColLabelAlignment(wxALIGN_LEFT, wxALIGN_CENTRE);
    GetGridWindow()->Bind(wxEVT_MOTION, &ALTraceGrid::onMouseMotion, this);
}

void ALTraceGrid::onResize(wxSizeEvent &event)
{
    // just extend the last column to fill out any available space in the window.
    wxGridUpdateLocker gridlock(this);
    const int w = GetClientSize().x - (GetRowLabelSize() + GetColSize(0) + GetColSize(1));
    if (w >= GetColMinimalWidth(2)) {
        SetColSize(2, w);
    }
    event.Skip();
}

void ALTraceGrid::onMouseMotion(wxMouseEvent &event)
{
    if (event.Dragging()) {
        event.Skip(false);  // don't let multiple rows select by dragging.
    }
}

void ALTraceGrid::onRowChosen(wxGridEvent &event)
{
    if (processing) {
        return;  // still loading the trace file.
    }

    const int row = event.GetRow();
    if (row < 0) {
        return;  // clicked on a column label.
    } else if (row == currentrow) {
        return;  // clicked on row that's already chosen.
    }

    currentrow = row;

    //printf("Clicked on grid column %d, row %d\n", (int) event.GetCol(), (int) event.GetRow());
    SelectRow(row);

    if ((GetGridCursorRow() != row) || (GetGridCursorCol() != 2)) {
        SetGridCursor(row, 2);
    }

    const ApiCallInfo *info = frame->getApiCallGridTable()->getApiCallInfo(row);
    frame->getCallInfoPage()->updateCallInfoPage(info);
    frame->getDeviceInfoPage()->updateItemList(info);
    frame->getContextInfoPage()->updateItemList(info);
    frame->getSourceInfoPage()->updateItemList(info);
    frame->getBufferInfoPage()->updateItemList(info);
}


class ALTraceGridUpdateLocker
{
public:
    ALTraceGridUpdateLocker(ALTraceGrid *_grid) : grid(_grid), updateLocker(_grid)
    {
        grid->setProcessing(true);
    }

    ~ALTraceGridUpdateLocker()
    {
        grid->setProcessing(false);
    }

private:
    ALTraceGrid *grid;
    wxGridUpdateLocker updateLocker;
};


void ALTraceHtmlWindow::onLinkClicked(wxHtmlLinkEvent& event)
{
    wxULongLong_t ullval = 0;
    wxStringTokenizer tokenizer(event.GetLinkInfo().GetHref(), "/");
    const wxString objtype(tokenizer.GetNextToken());
    tokenizer.GetNextToken();  // skip blank token from "://"
    tokenizer.GetNextToken().ToULongLong(&ullval);
    if (!ullval) {
        return;
    }
    const wxUIntPtr data = (wxUIntPtr) ullval;

    ALTraceListAndInfoPage *page = NULL;
    if (objtype == "source:") {
        page = frame->getSourceInfoPage();
    } else if (objtype == "buffer:") {
        page = frame->getBufferInfoPage();
    } else if (objtype == "context:") {
        page = frame->getContextInfoPage();
    } else if (objtype == "device:") {
        page = frame->getDeviceInfoPage();
    } else {
        return;
    }

    if (page->selectItemByData(data)) {
        frame->getNotebook()->SetSelection(frame->getNotebook()->FindPage(page));
    }
}

static wxString fontColorString(const char *color, const wxString &wrapme)
{
    return wxString::Format("<font bgcolor='#000000' color='%s'>%s</font>", color, wrapme);
}

static const wxString deviceAnchorTagString(const ALCdevice *dev, const wxString &wrapme)
{
    const wxUIntPtr data = (wxUIntPtr) dev;
    return wxString::Format("<a href='device://%llu'><uoff>%s</uoff></a>", (unsigned long long) data, wrapme);
}

static const wxString contextAnchorTagString(const ALCcontext *ctx, const wxString &wrapme)
{
    const wxUIntPtr data = (wxUIntPtr) ctx;
    return wxString::Format("<a href='context://%llu'><uoff>%s</uoff></a>", (unsigned long long) data, wrapme);
}

static const wxString sourceAnchorTagString(const StateTrie *trie, ALCcontext *ctx, const ALuint name, const wxString &wrapme)
{
    char buf[64];
    const uint64 *val = trie->getContextState(ctx, "device");
    if (!val || !*val) { return wrapme; }
    ALCdevice *dev = (ALCdevice *) *val;
    val = trie->getGlobalState("numdevices");
    if (!val) { return wrapme; }
    const uint64 numdevs = *val;
    uint64 devidx;
    for (devidx = 0; devidx < numdevs; devidx++) {
        snprintf(buf, sizeof (buf), "device/%u", (uint) devidx);
        val = trie->getGlobalState(buf);
        if (val && (*val == ((uint64) dev))) {
            break;
        }
    }
    if (devidx == numdevs) { return wrapme; }

    val = trie->getDeviceState(dev, "numcontexts");
    if (!val) { return wrapme; }
    const uint64 numctxs = *val;
    uint64 ctxidx;
    for (ctxidx = 0; ctxidx < numctxs; ctxidx++) {
        char buf[64];
        snprintf(buf, sizeof (buf), "context/%u", (uint) ctxidx);
        val = trie->getDeviceState(dev, buf);
        if (val && (*val == ((uint64) ctx))) {
            break;
        }
    }
    if (ctxidx == numdevs) { return wrapme; }

    const wxUIntPtr data = (devidx << 48) | (ctxidx << 32) | name;
    return wxString::Format("<a href='source://%llu'><uoff>%s</uoff></a>", (unsigned long long) data, wrapme);
}

static const wxString bufferAnchorTagString(const StateTrie *trie, ALCdevice *dev, const ALuint name, const wxString &wrapme)
{
    // !!! FIXME: code duplication with soutceAnchorTagString
    char buf[64];
    const uint64 *val = trie->getGlobalState("numdevices");
    if (!val) { return wrapme; }
    const uint64 numdevs = *val;
    uint64 devidx;
    for (devidx = 0; devidx < numdevs; devidx++) {
        snprintf(buf, sizeof (buf), "device/%u", (uint) devidx);
        val = trie->getGlobalState(buf);
        if (val && (*val == ((uint64) dev))) {
            break;
        }
    }
    if (devidx == numdevs) { return wrapme; }

    const wxUIntPtr data = (devidx << 32) | name;
    return wxString::Format("<a href='buffer://%llu'><uoff>%s</uoff></a>", (unsigned long long) data, wrapme);
}

static wxString htmlizeArgument(const ApiCallInfo *info, const ApiArgInfo *arg)
{
    switch (arg->type) {
        case ARG_device: {
            wxString retval;
            retval << fontColorString("#FF00FF", ptrString(arg->device));
            const uint64 *val = info->state->getDeviceState(arg->device, "label");
            if (val) {
                wxString str(wxT(" &lt;"));
                str << ((const char *) *val);
                str << wxT("&gt;");
                retval << fontColorString("#00FF00", str);
            }
            return deviceAnchorTagString(arg->device, retval);
        }

        case ARG_context: {
            wxString retval;
            retval << fontColorString("#FF00FF", ptrString(arg->context));
            const uint64 *val = info->state->getContextState(arg->context, "label");
            if (val) {
                wxString str(wxT(" &lt;"));
                str << ((const char *) *val);
                str << wxT("&gt;");
                retval << fontColorString("#00FF00", str);
            }
            return contextAnchorTagString(arg->context, retval);
        }

        case ARG_source: {
            ALCcontext *ctx = info->state->getCurrentContext();
            wxString retval;
            retval << fontColorString("#FF0000", wxString::Format("%u", (int) arg->source));
            const uint64 *val = ctx ? info->state->getSourceState(ctx, arg->source, "label") : NULL;
            if (val) {
                wxString str(wxT(" &lt;"));
                str << ((const char *) *val);
                str << wxT("&gt;");
                retval << fontColorString("#00FF00", str);
            }
            if (ctx) {
                return sourceAnchorTagString(info->state, ctx, arg->source, retval);
            }
            return retval;
        }

        case ARG_buffer: {
            ALCdevice *dev = NULL;
            ALCcontext *ctx = info->state->getCurrentContext(&dev);
            wxString retval;
            retval << fontColorString("#FF0000", wxString::Format("%u", (int) arg->buffer));
            const uint64 *val = (ctx && dev) ? info->state->getBufferState(dev, arg->buffer, "label") : NULL;
            if (val) {
                wxString str(wxT(" &lt;"));
                str << ((const char *) *val);
                str << wxT("&gt;");
                retval << fontColorString("#00FF00", str);
            }
            if (ctx && dev) {
                return bufferAnchorTagString(info->state, dev, arg->buffer, retval);
            }
            return retval;
        }

        case ARG_ptr: return fontColorString("#FF00FF", ptrString(arg->ptr));
        case ARG_sizei: return fontColorString("#FF0000", wxString::Format("%u", (uint) arg->sizei));
        case ARG_string: return fontColorString("#FFFF00", litString(arg->string));
        case ARG_alint: return fontColorString("#FF0000", wxString::Format("%d", (int) arg->alint));
        case ARG_aluint: return fontColorString("#FF0000", wxString::Format("%u", (uint) arg->aluint));
        case ARG_alfloat: return fontColorString("#FF0000", wxString::Format("%f", arg->alfloat));
        case ARG_alcenum: return fontColorString("#CCCCCC", alcenumString(arg->alcenum));
        case ARG_alenum: return fontColorString("#CCCCCC", alenumString(arg->alenum));
        case ARG_aldouble: return fontColorString("#FF0000", wxString::FromDouble(arg->aldouble));
        case ARG_alcbool: return fontColorString("#CCCCCC", alcboolString(arg->alcbool));
        case ARG_albool: return fontColorString("#CCCCCC", alboolString(arg->albool));
        default: break;
    }
    return wxT("???");
}

void ALTraceCallInfoPage::updateCallInfoPage(const ApiCallInfo *info)
{
    wxString html(wxString::Format(wxT("<html><body bgcolor='%s'><font color='%s'>"), getHtmlBackgroundColor(), getHtmlForegroundColor()));
    html << wxT("<p><h1>Function call</h1></p><p>\n");
    html << wxT("<font bgcolor='#000000' size='+1'><table width='100%' bgcolor='#000000'><tr><td colspan='2'>");

    html << fontColorString("#00FFFF", info->fnname);
    html << fontColorString("#00A0A1", "(");

    if (info->numargs > 0) {
        html << "</td></tr><tr>";
        for (int i = 0; i < info->numargs; i++) {
            const ApiArgInfo *arg = &info->arginfo[i];
            html << "<td>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";
            html << htmlizeArgument(info, arg);
            if (i < (info->numargs - 1)) {
                html << fontColorString("#00A0A1", ",");
            }

            html << "</td><td>";
            wxString comment(wxT("// "));
            comment << arg->name;
            html << fontColorString("#A0A100", comment);
            html << "</td></tr><tr>";
        }
        html << "<td colspan='2'>";
    }

    if (!info->retinfo) {
        html << fontColorString("#00A0A1", ");");
    } else if (info->numretinfo <= 1) {
        html << fontColorString("#00A0A1", ") => ");
        html << htmlizeArgument(info, info->retinfo);
        html << fontColorString("#00A0A1", ";");
    } else /*if (info->numretinfo >= 1)*/ {
        html << fontColorString("#00A0A1", ") => { ");
        for (int i = 0; i < info->numretinfo; i++) {
            html << htmlizeArgument(info, &info->retinfo[i]);
            if (i < (info->numretinfo-1)) {
                html << fontColorString("#00A0A1", ",");
            }
            html << wxT(" ");
        }
        html << fontColorString("#00A0A1", "};");
    }

    html << wxT("</td></tr></table></p><hr/>");

    if (info->generated_al_error) {
        html << wxT("<p><h1>AL error generated</h1></p><p><font size='+1'><ul>");
        html << wxT("<li>This call, or something related, triggered an AL error for the current context.");
        html << wxT(" Sometimes this is beyond your control (like AL_OUT_OF_MEMORY), but often this signifies");
        html << wxT(" a bug in your program to be fixed.");
        html << wxT("</li></ul></font></p>");
    }

    if (info->generated_alc_error) {
        html << wxT("<p><h1>ALC error generated</h1></p><p><font size='+1'><ul>");
        html << wxT("<li>This call, or something related, triggered an ALC error for the associated device.");
        html << wxT(" Sometimes this is beyond your control (like ALC_OUT_OF_MEMORY), but often this signifies");
        html << wxT(" a bug in your program to be fixed.");
        html << wxT("</li></ul></font></p>");
    }

    if (info->reported_failure && !info->generated_al_error && !info->generated_alc_error) {
        html << wxT("<p><h1>Failure reported</h1></p><p><font size='+1'><ul>");
        html << wxT("<li>This call returned an error code outside of alGetError(), or had some other basic issue");
        html << wxT(" that alTrace noticed.");
        html << wxT(" Sometimes this is beyond your control (like opening a device that fails at the OS level),");
        html << wxT(" but often this signifies a bug in your program to be fixed.");
        html << wxT("</li></ul></font></p>");
    }

    if (info->inefficient_state_change) {
        html << wxT("<p><h1>Inefficient call</h1></p><p><font size='+1'><ul>");
        html << wxT("<li>This call was ineffiencient or unnecessary. Often this means you tried to set a");
        html << wxT(" state to its current value, or you're calling alGetError() when nothing went wrong.");
        html << wxT(" Sometimes this is beyond your control, but often you can reduce or remove these calls");
        html << wxT(" that are doing useless work.");
        html << wxT("</li></ul></font></p>");
    }

    html << wxT("<p><h1>Callstack</h1></p><p><font size='+1'><ul>\n");
    for (int i = 0; i < info->num_callstack_frames; i++) {
        void *ptr = info->callstack[i].frame;
        const char *str = info->callstack[i].sym;
        html << wxT("<li>");
        html << (str ? str : ptrString(ptr));
        html << wxT("</li>");
    }
    html << wxT("</ul></font></p>");

    html << wxT("</font></body></html>");

    SetPage(html);
}

void ALTraceDeviceInfoPage::updateItemListImpl()
{
    const StateTrie *trie = apiinfo->state;
    const uint64 *val = trie->getGlobalState("numdevices");
    const uint64 numdevs = val ? *val : 0;
    for (uint64 i = 0; i < numdevs; i++) {
        char buf[64];
        snprintf(buf, sizeof (buf), "device/%u", (uint) i);
        val = trie->getGlobalState(buf);
        ALCdevice *dev = (ALCdevice *) (val ? *val : 0);
        if (dev) {
            wxString item(ptrString(dev));
            val = trie->getDeviceState(dev, "label");
            if (val) {
                item << wxT(" (\"");
                item << (const char *) *val;
                item << wxT("\")");
            }

            val = trie->getDeviceState(dev, "devtype");
            const uint64 devtype = val ? *val : 0xFFFFFFFF;
            if (devtype == 0) {  // !!! FIXME: make this an enum
                item << wxT(" [OUTPUT]");
            } else if (devtype == 1) {
                item << wxT(" [CAPTURE]");
            } else if (devtype == 2) {
                item << wxT(" [LOOPBACK]");
            }

            wxVector<wxVariant> row;
            row.push_back(wxVariant(item));
            itemlist->AppendItem(row, (wxUIntPtr) dev);
        }
    }
}

void ALTraceDeviceInfoPage::updateDetails(const wxUIntPtr data)
{
    const StateTrie *trie = apiinfo->state;
    ALCdevice *dev = (ALCdevice *) data;
    const uint64 *val;
    char buf[64];

    wxString html(wxString::Format(wxT("<html><body bgcolor='%s'><font color='%s'>"), details->getHtmlBackgroundColor(), details->getHtmlForegroundColor()));

    html << wxT("<p><h1>Device ");

    html << ptrString(dev);
    html << wxT("</h1></p>");

    html << wxT("<p><ul>");
        html << wxT("<li><strong>Label</strong>: ");
        val = trie->getDeviceState(dev, "label");
        if (val && *val) {
            html << wxT("\"");
            html << (const char *) *val;
            html << wxT("\"");
        } else {
            html << wxT("<i>none, try alcTraceDeviceLabel()!</i>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>Current error</strong>: ");
        val = trie->getDeviceState(dev, "error");
        html << alcenumString(val ? ((ALCenum) *val) : ALC_NO_ERROR);
        html << wxT("</li>");

        html << wxT("<li><strong>ALC_CONNECTED</strong>: ");
        val = trie->getDeviceState(dev, "ALC_CONNECTED");
        html << alcboolString(val ? ((ALCboolean) *val) : ALC_TRUE);
        html << wxT("</li>");

        html << wxT("<li><strong>Created contexts</strong>:");
        val = trie->getDeviceState(dev, "numcontexts");
        const uint64 numcontexts = val ? *val : 0;
        bool seen_ctxs = false;
        for (uint64 i = 0; i < numcontexts; i++) {
            snprintf(buf, sizeof (buf), "context/%u", (uint) i);
            val = trie->getDeviceState(dev, buf);
            ALCcontext *ctx = (ALCcontext *) (val ? *val : 0);
            if (ctx) {
                if (!seen_ctxs) {
                    html << wxT("<ol>");
                    seen_ctxs = true;
                }
                html << wxT("<li>");
                wxString ctxstr(ptrString(ctx));
                val = trie->getContextState(ctx, "label");
                if (val) {
                    ctxstr << wxT(" (\"");
                    ctxstr << (const char *) *val;
                    ctxstr << wxT("\")");
                }
                html << contextAnchorTagString(ctx, ctxstr);
                html << wxT("</li>");
            }
        }
        if (seen_ctxs) {
            html << wxT("</ol>");
        } else {
            html << wxT(" <i>none</i>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>Device type</strong>: ");
        val = trie->getDeviceState(dev, "devtype");
        const uint64 devtype = val ? *val : 0xFFFFFFFF;
        if (devtype == 0) {
            html << wxT("Output");
        } else if (devtype == 1) {
            html << wxT("Capture");
        } else if (devtype == 2) {
            html << wxT("Loopback");
        } else {
            html << wxT("<i>unknown</i>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>Device open string</strong>: ");
        val = trie->getDeviceState(dev, "openname");
        html << (val ? (litString((const char *) *val)) : "<i>unknown</i>");
        html << wxT("</li>");

        if (devtype == 1) {  // !!! FIXME: this should be an enum.
            html << wxT("<li><strong>ALC_CAPTURE_SAMPLES</strong>: ");
            val = trie->getDeviceState(dev, "ALC_CAPTURE_SAMPLES");
            union { uint64 ui64; ALCint i; } cvt; cvt.ui64 = (val ? *val : 0);
            html << cvt.i;
            html << wxT("</li>");

            html << wxT("<li><strong>Capturing started</strong>: ");
            val = trie->getDeviceState(dev, "capturing");
            html << alcboolString((ALCboolean) (val ? *val : 0));
            html << wxT("</li>");

            html << wxT("<li><strong>Device frequency</strong>: ");
            val = trie->getDeviceState(dev, "frequency");
            const uint64 pcmfreq = (val ? *val : 0);
            html << pcmfreq;
            html << wxT("</li>");

            html << wxT("<li><strong>Device format</strong>: ");
            val = trie->getDeviceState(dev, "format");
            const ALenum alfmt = val ? (ALenum) *val : AL_NONE;
            html << (val ? alenumString((ALenum) *val) : "<i>unknown</i>");
            html << wxT("</li>");

            html << wxT("<li><strong>Device buffer size (samples, not bytes!)</strong>: ");
            val = trie->getDeviceState(dev, "buffersize");
            html << (val ? *val : 0);
            html << wxT("</li>");

            // !!! FIXME: check current_player_id here and don't do file i/o if already matching.
            val = trie->getDeviceState(dev, "numcaptures");
            const uint64 numcaptures = val ? *val : 0;
            if (!numcaptures) {
                frame->clearAudio();
            } else {
                uint64 bufferlen = 0;
                for (uint64 i = 0; i < numcaptures; i++) {
                    snprintf(buf, sizeof (buf), "capturedatalen/%u", (uint) i);
                    val = trie->getDeviceState(dev, buf);
                    bufferlen += val ? *val : 0;
                }
                uint8 *pcm = new uint8[bufferlen];
                uint8 *pcmptr = pcm;
                const wxCharBuffer utf8path = frame->getTracefilePath().ToUTF8();
                FILE *f = fopen(utf8path.data(), "rb");  // !!! FIXME: don't use fopen.
                uint64 pcmoffset = 0;
                bool okay = false;
                if (f) {
                    for (uint64 i = 0; i < numcaptures; i++) {
                        okay = false;
                        snprintf(buf, sizeof (buf), "capturedatalen/%u", (uint) i);
                        val = trie->getDeviceState(dev, buf);
                        const uint64 len = val ? *val : 0;
                        if (len) {
                            snprintf(buf, sizeof (buf), "capturedata/%u", (uint) i);
                            val = trie->getDeviceState(dev, buf);
                            pcmoffset = val ? *val : 0;
                            if (fseek(f, (long) pcmoffset, SEEK_SET) != -1) {
                                if (fread(pcmptr, len, 1, f) == 1) {
                                    pcmptr += len;
                                    okay = true;
                                }
                            }
                            if (!okay) {
                                break;
                            }
                        }
                    }
                    fclose(f);
                }
                if (!okay) {
                    frame->clearAudio();
                } else {
                    frame->setAudio(pcmoffset, alfmt, pcm, bufferlen, pcmfreq);
                }
                delete[] pcm;
            }
        }

        if (devtype == 1) {  // !!! FIXME: this should be an enum.
            html << wxT("<li><strong>ALC_CAPTURE_DEVICE_SPECIFIER</strong>: ");
            val = trie->getDeviceState(dev, "ALC_CAPTURE_DEVICE_SPECIFIER");
        } else {
            html << wxT("<li><strong>ALC_DEVICE_SPECIFIER</strong>: ");
            val = trie->getDeviceState(dev, "ALC_DEVICE_SPECIFIER");
        }
        html << (val ? litString((const char *) *val) : "<i>unknown</i>");
        html << wxT("</li>");

        html << wxT("<li><strong>ALC_MAJOR_VERSION</strong>: ");
        val = trie->getDeviceState(dev, "ALC_MAJOR_VERSION");
        html << (val ? *val : 0);
        html << wxT("</li>");

        html << wxT("<li><strong>ALC_MINOR_VERSION</strong>: ");
        val = trie->getDeviceState(dev, "ALC_MINOR_VERSION");
        html << (val ? *val : 0);
        html << wxT("</li>");

        html << wxT("<li><strong>ALC_EXTENSIONS</strong>:");
        val = trie->getDeviceState(dev, "ALC_EXTENSIONS");
        char *ext = (char *) (val ? *val : 0);
        if (!ext) {
            html << wxT(" <i>none</i>");
        } else if (!*ext) {
            html << wxT(" <i>no extensions reported</i>");
        } else {
            html << wxT("<ol>");
            for (char *ptr = ext; *ptr; ptr++) {
                if (*ptr == ' ') {
                    html << wxT("<li>");
                    *ptr = '\0';
                    html << (const char *) ext;
                    *ptr = ' ';
                    html << wxT("</li>");
                    ext = ptr + 1;
                }
            }
            if (*ext) {
                html << wxT("<li>");
                html << ext;
                html << wxT("</li>");
            }
            html << wxT("</ol>");
        }
        html << wxT("</li>");

    html << wxT("</ul></p>");

    html << wxT("</font></body></html>");

    details->SetPage(html);
}

void ALTraceContextInfoPage::updateItemListImpl()
{
    const StateTrie *trie = apiinfo->state;
    ALCcontext *current = trie->getCurrentContext();
    const uint64 *val = trie->getGlobalState("numdevices");
    const uint64 numdevs = val ? *val : 0;
    char buf[64];

    for (uint64 i = 0; i < numdevs; i++) {
        snprintf(buf, sizeof (buf), "device/%u", (uint) i);
        val = trie->getGlobalState(buf);
        ALCdevice *dev = (ALCdevice *) (val ? *val : 0);
        if (dev) {
            val = trie->getDeviceState(dev, "numcontexts");
            const uint64 numctxs = val ? *val : 0;
            for (uint64 j = 0; j < numctxs; j++) {
                snprintf(buf, sizeof (buf), "context/%u", (uint) i);
                val = trie->getDeviceState(dev, buf);
                ALCcontext *ctx = (ALCcontext *) (val ? *val : 0);
                if (ctx) {
                    wxString item(ptrString(ctx));
                    val = trie->getContextState(ctx, "label");
                    if (val) {
                        item << " (\"";
                        item << (const char *) *val;
                        item << "\")";
                    }
                    if (current == ctx) {
                        item << wxT(" [CURRENT]");
                    }

                    wxVector<wxVariant> row;
                    row.push_back(wxVariant(item));
                    itemlist->AppendItem(row, (wxUIntPtr) ctx);
                }
            }
        }
    }
}

void ALTraceContextInfoPage::updateDetails(const wxUIntPtr data)
{
    const StateTrie *trie = apiinfo->state;
    ALCcontext *ctx = (ALCcontext *) data;
    union { uint64 ui64; float f; } cvt;
    const uint64 *val;
    char buf[64];

    wxString html(wxString::Format(wxT("<html><body bgcolor='%s'><font color='%s'>"), details->getHtmlBackgroundColor(), details->getHtmlForegroundColor()));
    html << wxT("<p><h1>Context ");
    html << ptrString(ctx);
    html << wxT("</h1></p>");

    html << wxT("<p><ul>");
        html << wxT("<li><strong>Label</strong>: ");
        val = trie->getContextState(ctx, "label");
        if (val && *val) {
            html << wxT("\"");
            html << (const char *) *val;
            html << wxT("\"");
        } else {
            html << wxT("<i>none, try alcTraceContextLabel()!</i>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>Is current context</strong>: ");
        html << alboolString(trie->getCurrentContext() == ctx);
        html << wxT("</li>");

        html << wxT("<li><strong>Current error</strong>: ");
        val = trie->getContextState(ctx, "error");
        html << alenumString(val ? ((ALenum) *val) : AL_NO_ERROR);
        html << wxT("</li>");

        html << wxT("<li><strong>Processing</strong>: ");
        val = trie->getContextState(ctx, "processing");
        html << alboolString((ALCboolean) (val ? *val : 0));
        html << wxT("</li>");

        html << wxT("<li><strong>Listener AL_POSITION</strong>: ");
        html << wxT("[ ");
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof (buf), "AL_POSITION/%d", i);
            val = trie->getContextState(ctx, buf);
            cvt.ui64 = val ? *val : 0;
            html << cvt.f;
            if (i < 2) {
                html << wxT(", ");
            }
        }
        html << wxT(" ]");
        html << wxT("</li>");

        html << wxT("<li><strong>Listener AL_VELOCITY</strong>: ");
        html << wxT("[ ");
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof (buf), "AL_VELOCITY/%d", i);
            val = trie->getContextState(ctx, buf);
            cvt.ui64 = val ? *val : 0;
            html << cvt.f;
            if (i < 2) {
                html << wxT(", ");
            }
        }
        html << wxT(" ]");
        html << wxT("</li>");

        html << wxT("<li><strong>Listener AL_ORIENTATION</strong>: ");
        html << wxT("[ ");
        for (int i = 0; i < 6; i++) {
            snprintf(buf, sizeof (buf), "AL_ORIENTATION/%d", i);
            val = trie->getContextState(ctx, buf);
            cvt.ui64 = val ? *val : 0;
            if (!val && (i == 2)) {
                cvt.f = -1.0f;
            } else if (!val && (i == 4)) {
                cvt.f = 1.0f;
            }
            html << cvt.f;
            if (i < 5) {
                html << wxT(", ");
            }
        }
        html << wxT(" ]");
        html << wxT("</li>");

        html << wxT("<li><strong>Listener AL_GAIN</strong>: ");
        val = trie->getContextState(ctx, "AL_GAIN");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_DISTANCE_MODEL</strong>: ");
        val = trie->getContextState(ctx, "AL_DISTANCE_MODEL");
        html << alenumString(val ? (ALenum) *val : AL_INVERSE_DISTANCE_CLAMPED);
        html << wxT("</li>");

        html << wxT("<li><strong>AL_DOPPLER_FACTOR</strong>: ");
        val = trie->getContextState(ctx, "AL_DOPPLER_FACTOR");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_DOPPLER_VELOCITY</strong>: ");
        val = trie->getContextState(ctx, "AL_DOPPLER_VELOCITY");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SPEED_OF_SOUND</strong>: ");
        val = trie->getContextState(ctx, "AL_SPEED_OF_SOUND");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 343.3f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>Device</strong>: ");
        val = trie->getContextState(ctx, "device");
        if (!val) {
            html << wxT("<i>unknown</i>");
        } else {
            ALCdevice *dev = (ALCdevice *) *val;
            wxString devstr(ptrString(dev));
            val = trie->getDeviceState(dev, "label");
            if (val && *val) {
                devstr << wxT(" (\"");
                devstr << (const char *) *val;
                devstr << wxT("\")");
            }
            html << deviceAnchorTagString(dev, devstr);
        }
        html << wxT("</li>");

        html << wxT("<li><strong>AL_VERSION</strong>: ");
        val = trie->getContextState(ctx, "AL_VERSION");
        if (!val) {
            html << wxT("<i>none, try alcMakeContextCurrent()!</i>");
        } else {
            html << litString((const char *) *val);
        }
        html << wxT("</li>");

        html << wxT("<li><strong>AL_VENDOR</strong>: ");
        val = trie->getContextState(ctx, "AL_VENDOR");
        if (!val) {
            html << wxT("<i>none, try alcMakeContextCurrent()!</i>");
        } else {
            html << litString((const char *) *val);
        }
        html << wxT("</li>");

        html << wxT("<li><strong>AL_RENDERER</strong>: ");
        val = trie->getContextState(ctx, "AL_RENDERER");
        if (!val) {
            html << wxT("<i>none, try alcMakeContextCurrent()!</i>");
        } else {
            html << litString((const char *) *val);
        }
        html << wxT("</li>");

        html << wxT("<li><strong>AL_EXTENSIONS</strong>:");
        val = trie->getContextState(ctx, "AL_EXTENSIONS");
        char *ext = (char *) (val ? *val : 0);
        if (!ext) {
            html << wxT(" <i>none, try alcMakeContextCurrent()!</i>");
        } else if (!*ext) {
            html << wxT(" <i>no extensions reported</i>");
        } else {
            html << wxT("<ol>");
            for (char *ptr = ext; *ptr; ptr++) {
                if (*ptr == ' ') {
                    html << wxT("<li>");
                    *ptr = '\0';
                    html << (const char *) ext;
                    *ptr = ' ';
                    html << wxT("</li>");
                    ext = ptr + 1;
                }
            }
            if (*ext) {
                html << wxT("<li>");
                html << ext;
                html << wxT("</li>");
            }
            html << wxT("</ol>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>ALC_ATTRIBUTES_SIZE</strong>: ");
        val = trie->getContextState(ctx, "ALC_ATTRIBUTES_SIZE");
        const uint64 attrsize = val ? *val : 0;
        html << attrsize;
        html << wxT("</li>");

        html << wxT("<li><strong>ALC_ALL_ATTRIBUTES</strong>:");
        if (attrsize == 0) {
            html << wxT(" <i>none</i>");
        } else {
            html << wxT("<ol>");
            bool isparam = true;
            for (uint64 i = 0; i < attrsize; i++) {
                snprintf(buf, sizeof (buf), "ALC_ALL_ATTRIBUTES/%u", (uint) i);
                val = trie->getContextState(ctx, buf);
                i++;
                const uint64 x = val ? *val : 0;
                html << wxT("<li>");
                if (isparam) {
                    html << alcenumString(x);
                } else {
                    union { ALCint i; uint64 ui64; } icvt; icvt.ui64 = x;
                    html << icvt.i;
                }
                html << wxT("</li>");
                isparam = !isparam;
            }
            html << wxT("</ol>");
        }
        html << wxT("</li>");


// !!! FIXME
//    ALsource *playlist;  /* linked list of currently-playing sources. Mixer thread only! */

    html << wxT("</ul></p>");

    html << wxT("</font></body></html>");

    details->SetPage(html);
}

void ALTraceSourceInfoPage::updateItemListImpl()
{
    const StateTrie *trie = apiinfo->state;
    const uint64 *val = trie->getGlobalState("numdevices");
    const uint64 numdevs = val ? *val : 0;
    char buf[64];

    for (uint64 i = 0; i < numdevs; i++) {
        snprintf(buf, sizeof (buf), "device/%u", (uint) i);
        val = trie->getGlobalState(buf);
        ALCdevice *dev = (ALCdevice *) (val ? *val : 0);
        if (!dev) {
            continue;
        }

        val = trie->getDeviceState(dev, "numcontexts");
        const uint64 numctxs = val ? *val : 0;
        for (uint64 j = 0; j < numctxs; j++) {
            snprintf(buf, sizeof (buf), "context/%u", (uint) j);
            val = trie->getDeviceState(dev, buf);
            ALCcontext *ctx = (ALCcontext *) (val ? *val : 0);
            if (!ctx) {
                continue;
            }

            wxString ctxstr(" in Context ");
            ctxstr << ptrString(ctx);
            val = trie->getContextState(ctx, "label");
            if (val) {
                ctxstr << " (\"";
                ctxstr << (const char *) *val;
                ctxstr << "\")";
            }

            // !!! FIXME: store these as ranges of sources to save memory and lookups at some point.
            val = trie->getContextState(ctx, "numsources");
            const uint64 numsrcs = val ? *val : 0;
            for (uint64 z = 0; z < numsrcs; z++) {
                snprintf(buf, sizeof (buf), "source/%u", (uint) z);
                val = trie->getContextState(ctx, buf);
                const uint64 name = val ? *val : 0;
                if (!name) {
                    continue;
                }

                val = trie->getSourceState(ctx, name, "allocated");
                if (!val || !*val) {
                    continue;
                }

                wxString item("Source ");
                item << name;
                val = trie->getSourceState(ctx, name, "label");
                if (val) {
                    item << " (\"";
                    item << (const char *) *val;
                    item << "\")";
                }
                item << ctxstr;

                val = trie->getSourceState(ctx, name, "AL_SOURCE_STATE");
                if (val && (*val == AL_PLAYING)) {
                    item << " [PLAYING]";
                }

                wxVector<wxVariant> row;
                row.push_back(wxVariant(item));
                itemlist->AppendItem(row, (wxUIntPtr) ((i << 48) | (j << 32) | name));
            }
        }
    }
}

void ALTraceSourceInfoPage::updateDetails(const wxUIntPtr data)
{
    const StateTrie *trie = apiinfo->state;
    const uint32 devidx = (uint32) ((data >> 48) & 0xFFFF);
    const uint32 ctxidx = (uint32) ((data >> 32) & 0xFFFF);
    const uint32 name = (uint32) (data & 0xFFFFFFFF);

    union { ALfloat f; uint64 ui64; } cvt;
    union { ALint i; uint64 ui64; } cvti;
    const uint64 *val;
    char buf[64];

    snprintf(buf, sizeof (buf), "device/%u", (uint) devidx);
    val = trie->getGlobalState(buf);
    if (!val) {  // uhoh!
        clearDetails();
        return;
    }
    ALCdevice *dev = (ALCdevice *) *val;

    snprintf(buf, sizeof (buf), "context/%u", (uint) ctxidx);
    val = trie->getDeviceState(dev, buf);
    if (!val) {  // uhoh!
        clearDetails();
        return;
    }
    ALCcontext *ctx = (ALCcontext *) *val;

    wxString html(wxString::Format(wxT("<html><body bgcolor='%s'><font color='%s'>"), details->getHtmlBackgroundColor(), details->getHtmlForegroundColor()));
    html << wxT("<p><h1>Source ");
    html << name;
    html << wxT("</h1></p>");

    html << wxT("<p><ul>");
        html << wxT("<li><strong>Label</strong>: ");
        val = trie->getSourceState(ctx, name, "label");
        if (val && *val) {
            html << wxT("\"");
            html << (const char *) *val;
            html << wxT("\"");
        } else {
            html << wxT("<i>none, try alcTraceSourceLabel()!</i>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SOURCE_STATE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_SOURCE_STATE");
        html << alenumString(val ? ((ALenum) *val) : AL_INITIAL);
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SOURCE_TYPE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_SOURCE_TYPE");
        html << alenumString(val ? ((ALenum) *val) : AL_UNDETERMINED);
        html << wxT("</li>");

        html << wxT("<li><strong>AL_BUFFER</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_BUFFER");
        const uint32 buffername = (uint32) (val ? *val : 0);
        if (!buffername) {
            html << buffername;
        } else {
            wxString bufstr;
            bufstr << buffername;
            val = trie->getBufferState(dev, buffername, "label");
            if (val && *val) {
                bufstr << wxT(" (\"");
                bufstr << (const char *) *val;
                bufstr << wxT("\")");
            }
            html << bufferAnchorTagString(trie, dev, buffername, bufstr);
        }
        html << wxT("</li>");

        html << wxT("<li><strong>AL_BUFFERS_QUEUED</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_BUFFERS_QUEUED");
        cvti.ui64 = val ? *val : 0;
        html << cvti.i;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_BUFFERS_PROCESSED</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_BUFFERS_PROCESSED");
        cvti.ui64 = val ? *val : 0;
        html << cvti.i;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_POSITION</strong>: ");
        html << wxT("[ ");
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof (buf), "AL_POSITION/%d", i);
            val = trie->getSourceState(ctx, name, buf);
            cvt.ui64 = val ? *val : 0;
            html << cvt.f;
            if (i < 2) {
                html << wxT(", ");
            }
        }
        html << wxT(" ]");
        html << wxT("</li>");

        html << wxT("<li><strong>AL_DIRECTION</strong>: ");
        html << wxT("[ ");
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof (buf), "AL_DIRECTION/%d", i);
            val = trie->getSourceState(ctx, name, buf);
            cvt.ui64 = val ? *val : 0;
            html << cvt.f;
            if (i < 2) {
                html << wxT(", ");
            }
        }
        html << wxT(" ]");
        html << wxT("</li>");

        html << wxT("<li><strong>AL_VELOCITY</strong>: ");
        html << wxT("[ ");
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof (buf), "AL_VELOCITY/%d", i);
            val = trie->getSourceState(ctx, name, buf);
            cvt.ui64 = val ? *val : 0;
            html << cvt.f;
            if (i < 2) {
                html << wxT(", ");
            }
        }
        html << wxT(" ]");
        html << wxT("</li>");

        html << wxT("<li><strong>AL_LOOPING</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_LOOPING");
        html << alboolString(val ? *val : AL_FALSE);
        html << wxT("</li>");

        html << wxT("<li><strong>AL_GAIN</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_GAIN");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_PITCH</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_PITCH");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SEC_OFFSET</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_SEC_OFFSET");
        cvti.ui64 = val ? *val : 0;
        html << cvti.i;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SAMPLE_OFFSET</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_SAMPLE_OFFSET");
        cvti.ui64 = val ? *val : 0;
        html << cvti.i;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_BYTE_OFFSET</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_BYTE_OFFSET");
        cvti.ui64 = val ? *val : 0;
        html << cvti.i;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SOURCE_RELATIVE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_SOURCE_RELATIVE");
        html << alboolString(val ? *val : AL_FALSE);
        html << wxT("</li>");

        html << wxT("<li><strong>AL_REFERENCE_DISTANCE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_REFERENCE_DISTANCE");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_ROLLOFF_FACTOR</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_ROLLOFF_FACTOR");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_MAX_DISTANCE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_MAX_DISTANCE");
        if (val) { cvt.ui64 = *val; } else { cvt.f = FLT_MAX; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_CONE_INNER_ANGLE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_CONE_INNER_ANGLE");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 360.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_CONE_OUTER_ANGLE</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_CONE_OUTER_ANGLE");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 360.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_CONE_OUTER_GAIN</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_CONE_OUTER_GAIN");
        cvt.ui64 = val ? *val : 0;
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_MIN_GAIN</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_MIN_GAIN");
        cvt.ui64 = val ? *val : 0;
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_MAX_GAIN</strong>: ");
        val = trie->getSourceState(ctx, name, "AL_MAX_GAIN");
        if (val) { cvt.ui64 = *val; } else { cvt.f = 1.0f; }
        html << cvt.f;
        html << wxT("</li>");

        html << wxT("<li><strong>Context</strong>: ");
        wxString ctxstr(ptrString(ctx));
        val = trie->getContextState(ctx, "label");
        if (val && *val) {
            ctxstr << wxT(" (\"");
            ctxstr << (const char *) *val;
            ctxstr << wxT("\")");
        }
        html << contextAnchorTagString(ctx, ctxstr);
        html << wxT("</li>");

    html << wxT("</ul></p>");

    html << wxT("</font></body></html>");

    details->SetPage(html);
}

void ALTraceBufferInfoPage::updateItemListImpl()
{
    const StateTrie *trie = apiinfo->state;
    const uint64 *val = trie->getGlobalState("numdevices");
    const uint64 numdevs = val ? *val : 0;
    char buf[64];

    for (uint64 i = 0; i < numdevs; i++) {
        snprintf(buf, sizeof (buf), "device/%u", (uint) i);
        val = trie->getGlobalState(buf);
        ALCdevice *dev = (ALCdevice *) (val ? *val : 0);
        if (!dev) {
            continue;
        }

        wxString devstr(" in Device ");
        devstr << ptrString(dev);
        val = trie->getDeviceState(dev, "label");
        if (val) {
            devstr << " (\"";
            devstr << (const char *) *val;
            devstr << "\")";
        }

        // !!! FIXME: store these as ranges of buffers to save memory and lookups at some point.
        val = trie->getDeviceState(dev, "numbuffers");
        const uint64 numbufs = val ? *val : 0;
        for (uint64 z = 0; z < numbufs; z++) {
            snprintf(buf, sizeof (buf), "buffer/%u", (uint) z);
            val = trie->getDeviceState(dev, buf);
            const uint64 name = val ? *val : 0;
            if (!name) {
                continue;
            }

            val = trie->getBufferState(dev, name, "allocated");
            if (!val || !*val) {
                continue;
            }

            wxString item("Buffer ");
            item << name;
            val = trie->getBufferState(dev, name, "label");
            if (val) {
                item << " (\"";
                item << (const char *) *val;
                item << "\")";
            }
            item << devstr;

            wxVector<wxVariant> row;
            row.push_back(wxVariant(item));
            itemlist->AppendItem(row, (wxUIntPtr) ((i << 32) | name));
        }
    }
}

void ALTraceBufferInfoPage::clearDetails()
{
    ALTraceListAndInfoPage::clearDetails();
    frame->clearAudio();
}

void ALTraceBufferInfoPage::updateDetails(const wxUIntPtr data)
{
    const StateTrie *trie = apiinfo->state;
    const uint32 devidx = (uint32) (data >> 32);
    const uint32 name = (uint32) (data & 0xFFFFFFFF);
    union { ALint i; uint64 ui64; } cvt;
    const uint64 *val;
    char buf[64];

    snprintf(buf, sizeof (buf), "device/%u", (uint) devidx);
    val = trie->getGlobalState(buf);
    if (!val) {  // uhoh!
        clearDetails();
        return;
    }
    ALCdevice *dev = (ALCdevice *) *val;

    wxString html(wxString::Format(wxT("<html><body bgcolor='%s'><font color='%s'>"), details->getHtmlBackgroundColor(), details->getHtmlForegroundColor()));
    html << wxT("<p><h1>Buffer ");
    html << name;
    html << wxT("</h1></p>");

    html << wxT("<p><ul>");
        html << wxT("<li><strong>Label</strong>: ");
        val = trie->getBufferState(dev, name, "label");
        if (val && *val) {
            html << wxT("\"");
            html << (const char *) *val;
            html << wxT("\"");
        } else {
            html << wxT("<i>none, try alcTraceBufferLabel()!</i>");
        }
        html << wxT("</li>");

        html << wxT("<li><strong>Format</strong>: ");
        val = trie->getBufferState(dev, name, "format");
        const ALenum alfmt = val ? ((ALenum) *val) : AL_NONE;
        html << alenumString(alfmt);
        html << wxT("</li>");

        html << wxT("<li><strong>AL_FREQUENCY</strong>: ");
        val = trie->getBufferState(dev, name, "AL_FREQUENCY");
        if (val) { cvt.ui64 = *val; } else { cvt.i = 0; }
        const ALint pcmfreq = cvt.i;
        html << pcmfreq;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_SIZE</strong>: ");
        val = trie->getBufferState(dev, name, "AL_SIZE");
        if (val) { cvt.ui64 = *val; } else { cvt.i = 0; }
        html << cvt.i;
        html << wxT(" bytes");
        html << wxT("</li>");

        html << wxT("<li><strong>AL_BITS</strong>: ");
        val = trie->getBufferState(dev, name, "AL_BITS");
        if (val) { cvt.ui64 = *val; } else { cvt.i = 16; }
        html << cvt.i;
        html << wxT("</li>");

        html << wxT("<li><strong>AL_CHANNELS</strong>: ");
        val = trie->getBufferState(dev, name, "AL_CHANNELS");
        if (val) { cvt.ui64 = *val; } else { cvt.i = 1; }
        html << cvt.i;
        html << wxT("</li>");

        html << wxT("<li><strong>Device</strong>: ");
        wxString devstr(ptrString(dev));
        val = trie->getDeviceState(dev, "label");
        if (val && *val) {
            devstr << wxT(" (\"");
            devstr << (const char *) *val;
            devstr << wxT("\")");
        }
        html << deviceAnchorTagString(dev, devstr);
        html << wxT("</li>");

        val = trie->getBufferState(dev, name, "datalen");
        const size_t pcmlen = (const size_t) (val ? *val : 0);
        val = trie->getBufferState(dev, name, "data");
        const uint64 pcmoffset = val ? *val : 0;
        if (frame->getCurrentPlayerId() != pcmoffset) {  // only read from disk if this is different.
            uint8 *pcm = NULL;
            bool okay = false;
            if (alfmt && pcmfreq && pcmoffset && pcmlen) {
                pcm = new uint8[pcmlen];
            }
            if (pcm) {
                const wxCharBuffer utf8path = frame->getTracefilePath().ToUTF8();
                FILE *f = fopen(utf8path.data(), "rb");  // !!! FIXME: don't use fopen.
                if (f) {
                    if (fseek(f, (long) pcmoffset, SEEK_SET) != -1) {
                        if (fread(pcm, pcmlen, 1, f) == 1) {
                            okay = true;
                        }
                    }
                    fclose(f);
                }

                if (okay) {
                    frame->setAudio(pcmoffset, alfmt, pcm, pcmlen, pcmfreq);
                }

                delete[] pcm;
            }

            if (!okay) {
                frame->clearAudio();
            }
        }

    html << wxT("</ul></p>");

    html << wxT("</font></body></html>");

    details->SetPage(html);
}

ApiCallInfo *ALTraceGridTable::getApiCallInfo(const int _row)
{
    const int row = (_row < 0) ? (numrows + _row) : _row;
    return ((row >= 0) && (row < numrows)) ? infoarray[row] : NULL;
}

void ALTraceGridTable::appendApiCall(ApiCallInfo *info, const CallerInfo *callerinfo)
{
    const int row = numrows;

    // allocate these in blocks so we don't realloc every time.
    if ((row % 256) == 0) {
        void *ptr = realloc(infoarray, (row + 256) * sizeof (ApiCallInfo *));
        if (!ptr) {
            out_of_memory();
        }
        infoarray = (ApiCallInfo **) ptr;
        //memset(&infoarray[row+1], '\0', 255 * sizeof (ApiCallInfo *));
    }
    infoarray[row] = info;

    numrows++;

    const size_t indent = callerinfo->trace_scope * 5;
    wxString str;
    for (size_t i = 0; i < indent; i++) {
        str << wxT(" ");
    }
    str << info->fnname;
    str << wxT("(");

    for (int i = 0; i < info->numargs; i++) {
        const ApiArgInfo *arg = &info->arginfo[i];
        switch (arg->type) {
            case ARG_device: str << deviceString(arg->device); break;
            case ARG_context: str << ctxString(arg->context); break;
            case ARG_source: str << sourceString(arg->source); break;
            case ARG_buffer: str << bufferString(arg->buffer); break;
            case ARG_ptr: str << ptrString(arg->ptr); break;
            case ARG_sizei: str << arg->sizei; break;
            case ARG_string: str << litString(arg->string); break;
            case ARG_alint: str << arg->alint; break;
            case ARG_aluint: str << arg->aluint; break;
            case ARG_alfloat: str << arg->alfloat; break;
            case ARG_alcenum: str << alcenumString(arg->alcenum); break;
            case ARG_alenum: str << alenumString(arg->alenum); break;
            case ARG_aldouble: str << arg->aldouble; break;
            case ARG_alcbool: str << alcboolString(arg->alcbool); break;
            case ARG_albool: str << alboolString(arg->albool); break;
            default: str << wxT("???"); break;
        }

        if (i < (info->numargs - 1)) {
            str << wxT(", ");
        }
    }

    str << wxT(")");

    info->callstr = cache_string(static_cast<const char*>(str.c_str()));

    if (latestCallTime < info->timestamp) {
        latestCallTime = info->timestamp;
    }

    if (largestThreadNum < info->threadid) {
        largestThreadNum = info->threadid;
    }
}


ALTraceFrame::ALTraceFrame()
    : wxFrame(NULL, -1, wxT("alTrace"), getPreviousPos(), getPreviousSize())
    , topSplit(NULL)
    , infoSplit(NULL)
    , apiCallGridTable(NULL)
    , apiCallGrid(NULL)
    , stateNotebook(NULL)
    , callInfoPage(NULL)
    , deviceInfoPage(NULL)
    , sourceInfoPage(NULL)
    , bufferInfoPage(NULL)
    , contextInfoPage(NULL)
    , audioPlayer(NULL)
    , current_player_id(0)
    , nonMaximizedX(0)
    , nonMaximizedY(0)
    , nonMaximizedWidth(0)
    , nonMaximizedHeight(0)
{
    GetPosition(&nonMaximizedX, &nonMaximizedY);
    GetSize(&nonMaximizedWidth, &nonMaximizedHeight);

    long pos = 0;
    wxConfigBase *cfg = wxConfig::Get();
    const wxSize clientSize(GetClientSize());

    topSplit = new wxSplitterWindow(this, -1, wxDefaultPosition, clientSize, wxSP_3D | wxSP_LIVE_UPDATE);
    topSplit->SetSashGravity(0.5);
    topSplit->SetMinimumPaneSize(1);

    // The sizer just makes sure that topSplit owns whole client area.
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(topSplit, 1, wxALL | wxEXPAND /*| wxALIGN_CENTRE*/);
    sizer->SetItemMinSize(topSplit, 1, 1);
    SetSizer(sizer);

    infoSplit = new wxSplitterWindow(topSplit, -1, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE);
    infoSplit->SetSashGravity(0.5);
    infoSplit->SetMinimumPaneSize(1);


    apiCallGridTable = new ALTraceGridTable;
    apiCallGrid = new ALTraceGrid(this, apiCallGridTable, infoSplit);

    const int notebookid = wxNewId();
    stateNotebook = new wxNotebook(infoSplit, notebookid);
    Connect(notebookid, wxEVT_NOTEBOOK_PAGE_CHANGED, wxBookCtrlEventHandler(ALTraceFrame::onNotebookPageChanged));

    callInfoPage = new ALTraceCallInfoPage(this, stateNotebook, wxID_ANY);
    sourceInfoPage = new ALTraceSourceInfoPage(this, stateNotebook, wxID_ANY);
    bufferInfoPage = new ALTraceBufferInfoPage(this, stateNotebook, wxID_ANY);
    contextInfoPage = new ALTraceContextInfoPage(this, stateNotebook, wxID_ANY);
    deviceInfoPage = new ALTraceDeviceInfoPage(this, stateNotebook, wxID_ANY);

    stateNotebook->AddPage(callInfoPage, wxT("Call details"));
    stateNotebook->AddPage(sourceInfoPage, wxT("Sources"));
    stateNotebook->AddPage(bufferInfoPage, wxT("Buffers"));
    stateNotebook->AddPage(contextInfoPage, wxT("Contexts"));
    stateNotebook->AddPage(deviceInfoPage, wxT("Devices"));

    infoSplit->SplitVertically(apiCallGrid, stateNotebook);
    if ( (cfg != NULL) && (cfg->Read(wxT("InfoSplitPos"), &pos)) ) {
        infoSplit->SetSashPosition((int) pos);
    }

    audioPlayer = new ALTraceAudioPlayerCtrl(topSplit, wxID_ANY);

    topSplit->SplitHorizontally(infoSplit, audioPlayer);
    if ( (cfg != NULL) && (cfg->Read(wxT("TopSplitPos"), &pos)) ) {
        topSplit->SetSashPosition((int) pos);
    } else {
        topSplit->SetSashPosition((int) (clientSize.GetHeight() * 0.90f));
    }

    long mx = 0;
    if ( (cfg != NULL) && (cfg->Read(wxT("Maximized"), &mx)) && (mx) ) {
        Maximize();
    }

    apiCallGrid->SetFocus();
}

ALTraceFrame::~ALTraceFrame()
{
    if (apiCallGrid) {
        apiCallGrid->SetTable(NULL, false, wxGrid::wxGridSelectRows);
    }
    delete apiCallGridTable;
}

const wxPoint ALTraceFrame::getPreviousPos()
{
return wxDefaultPosition;
#if 0
    int dpyw, dpyh;
    ::wxDisplaySize(&dpyw, &dpyh);
    long winx, winy, winw, winh;
    wxConfigBase *cfg = wxConfig::Get();
    if ( (cfg == NULL) ||
         (!cfg->Read(wxT("WindowX"), &winx)) ||
         (!cfg->Read(wxT("WindowY"), &winy)) ||
         (!cfg->Read(wxT("WindowW"), &winw)) ||
         (!cfg->Read(wxT("WindowH"), &winh)) )
        return wxPoint(dpyw / 8, dpyh / 8);
    if (winw > dpyw) { winw = dpyw; winx = 0; }
    else if (winw < 50) winw = 50;
    if (winh > dpyh) { winh = dpyh; winy = 0; }
    else if (winh < 50) winh = 50;
    if (winx+winw < 10) winx = 0;
    else if (winx > dpyw-10) winx = dpyw-10;
    if (winy < 0) winy = 0;
    else if (winy > dpyh-10) winy = dpyh-10;
    return wxPoint(winx, winy);
#endif
}

const wxSize ALTraceFrame::getPreviousSize()
{
    int dpyw, dpyh;
    ::wxDisplaySize(&dpyw, &dpyh);
    long winw, winh;
    wxConfigBase *cfg = wxConfig::Get();
    if ( (cfg == NULL) ||
         (!cfg->Read(wxT("WindowW"), &winw)) ||
         (!cfg->Read(wxT("WindowH"), &winh)) ) {
        return wxSize(dpyw - (dpyw / 4), dpyh - (dpyh / 4));
    }
    if (winw > dpyw) winw = dpyw;
    else if (winw < 50) winw = 50;
    if (winh > dpyh) winh = dpyh;
    else if (winh < 50) winh = 50;
    return wxSize(winw, winh);
}

// We have a problem (at least on wxCocoa...?) that the details window won't
//  redraw when changing pages, so we force it here. It seems to be something
//  about using a wxHtmlWindow as a child of a wxSplitterWindow. Other HTML
//  windows are fine, as are other children in a splitter. Beats me. Forcing
//  it here fixes it.
void ALTraceFrame::onNotebookPageChanged(wxBookCtrlEvent& event)
{
    switch (event.GetSelection()) {
        case 1: sourceInfoPage->forceDetailsRedraw(); break;
        case 2: bufferInfoPage->forceDetailsRedraw(); break;
        case 3: contextInfoPage->forceDetailsRedraw(); break;
        case 4: deviceInfoPage->forceDetailsRedraw(); break;
        default: break;
    }
}

void ALTraceFrame::onSysColourChanged(wxSysColourChangedEvent& event)
{
    apiCallGridTable->onSysColourChanged(event);
    apiCallGrid->Refresh();
    const int row = apiCallGrid->getCurrentRow();
    if (row < 0) {
        callInfoPage->resetPage();
    } else {
        callInfoPage->updateCallInfoPage(apiCallGridTable->getApiCallInfo(row));
    }
    event.Skip();  // make sure everything else catches this one, too.
}

void ALTraceFrame::onResize(wxSizeEvent &event)
{
    if (!IsMaximized()) {
        GetSize(&nonMaximizedWidth, &nonMaximizedHeight);
        GetPosition(&nonMaximizedX, &nonMaximizedY);
    }
    event.Skip();
}

void ALTraceFrame::onMove(wxMoveEvent &event)
{
    if (!IsMaximized()) {
        GetPosition(&nonMaximizedX, &nonMaximizedY);
    }
}

void ALTraceFrame::onClose(wxCloseEvent &event)
{
    wxConfigBase *cfg = wxConfig::Get();
    if (cfg != NULL) {
        cfg->Write(wxT("WindowW"), (long) nonMaximizedWidth);
        cfg->Write(wxT("WindowH"), (long) nonMaximizedHeight);
        cfg->Write(wxT("WindowX"), (long) nonMaximizedX);
        cfg->Write(wxT("WindowY"), (long) nonMaximizedY);
        cfg->Write(wxT("Maximized"), (long) (IsMaximized() ? 1 : 0));
        cfg->Write(wxT("InfoSplitPos"), (long) infoSplit->GetSashPosition());
        cfg->Write(wxT("TopSplitPos"), (long) topSplit->GetSashPosition());
    }
    Destroy();
}

void ALTraceFrame::setAudio(const uint64 playerid, const ALenum alfmt, const void *pcm, const size_t pcmbytes, const unsigned int freq)
{
    if (playerid == current_player_id) {
        return;  // already set.
    }

    current_player_id = playerid;

    if (!pcm || !pcmbytes) {
        clearAudio();
        return;
    }

    switch (alfmt) {
        case AL_FORMAT_MONO8:
            audioPlayer->setAudio((const uint8 *) pcm, pcmbytes, 1, freq);
            break;
        case AL_FORMAT_MONO16:
            audioPlayer->setAudio((const int16 *) pcm, pcmbytes, 1, freq);
            break;
        case AL_FORMAT_MONO_FLOAT32:
            audioPlayer->setAudio((const float *) pcm, pcmbytes, 1, freq);
            break;
        case AL_FORMAT_STEREO8:
            audioPlayer->setAudio((const uint8 *) pcm, pcmbytes, 2, freq);
            break;
        case AL_FORMAT_STEREO16:
            audioPlayer->setAudio((const int16 *) pcm, pcmbytes, 2, freq);
            break;
        case AL_FORMAT_STEREO_FLOAT32:
            audioPlayer->setAudio((const float *) pcm, pcmbytes, 2, freq);
            break;
        default:
            clearAudio();
            break;
    }
}

void ALTraceFrame::clearAudio()
{
    audioPlayer->clearAudio();
    current_player_id = 0;
}



struct VisitArgs
{
    ALTraceFrame *frame;
    wxProgressDialog *progressdlg;
    ApiCallInfo *info;
    int lastprogresspct;
    uint32 nextprogressticks;
    int longestcallstr_width;
    const char *longestcallstr;
};


#define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) static void make_state_##name visitparams;
#include "altrace_entrypoints.h"


// Visitors for converting api call arguments to ApiCallInfo.

#define START_ARGS() VisitArgs *visitargs = (VisitArgs *) callerinfo->userdata; ApiCallInfo *info = visitargs->info; (void) info; int argidx = 0; (void) argidx;
#define SET_ARGINFO(typ, val, desc) { ApiArgInfo *arg = &info->arginfo[argidx++]; arg->name = desc; arg->type = ARG_##typ; arg->typ = val; }
#define SET_RETINFO(typ) { info->retinfo = new ApiArgInfo; info->numretinfo = 0; info->retinfo->name = "return value"; info->retinfo->type = ARG_##typ; info->retinfo->typ = retval; }
#define SET_RETINFOCOUNT(n) { info->retinfo = new ApiArgInfo[n]; info->numretinfo = n; }
#define SET_RETINFOn(n, typ, val) { info->retinfo[n].name = "return value"; info->retinfo[n].type = ARG_##typ; info->retinfo[n].typ = val; }

static void make_state_alcGetCurrentContext(CallerInfo *callerinfo, ALCcontext *retval)
{
    START_ARGS();
    SET_RETINFO(context);
}

static void make_state_alcGetContextsDevice(CallerInfo *callerinfo, ALCdevice *retval, ALCcontext *ctx)
{
    START_ARGS();
    SET_ARGINFO(context, ctx, "context to query");
    SET_RETINFO(device);
    if (!retval) {
        info->reported_failure = AL_TRUE;
    }
}

static void make_state_alcIsExtensionPresent(CallerInfo *callerinfo, ALCboolean retval, ALCdevice *device, const ALCchar *extname)
{
    extname = cache_string(extname);
    START_ARGS();
    SET_ARGINFO(device, device, "device to query");
    SET_ARGINFO(string, extname, "extension name");
    SET_RETINFO(alcbool);
}

static void make_state_alcGetProcAddress(CallerInfo *callerinfo, void *retval, ALCdevice *device, const ALCchar *funcname)
{
    funcname = cache_string(funcname);
    START_ARGS();
    SET_ARGINFO(device, device, "device to query");
    SET_ARGINFO(string, funcname, "function name");
    SET_RETINFO(ptr);
    if (!retval) {
        info->reported_failure = AL_TRUE;
    }
}

static void make_state_alcGetEnumValue(CallerInfo *callerinfo, ALCenum retval, ALCdevice *device, const ALCchar *enumname)
{
    enumname = cache_string(enumname);
    START_ARGS();
    SET_ARGINFO(device, device, "device to query");
    SET_ARGINFO(string, enumname, "enum name");
    SET_RETINFO(alcenum);
}

static void make_state_alcGetString(CallerInfo *callerinfo, const ALCchar *retval, ALCdevice *device, ALCenum param)
{
    retval = cache_string(retval);
    START_ARGS();
    SET_ARGINFO(device, device, "device to query");
    SET_ARGINFO(alcenum, param, "parameter");
    SET_RETINFO(string);
}

static void make_state_alcCaptureOpenDevice(CallerInfo *callerinfo, ALCdevice *retval, const ALCchar *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize, ALint major_version, ALint minor_version, const ALCchar *devspec, const ALCchar *extensions)
{
    devicename = cache_string(devicename);
    devspec = cache_string(devspec);
    extensions = cache_string(extensions);
    START_ARGS();
    SET_ARGINFO(string, devicename, "device name to open");
    SET_ARGINFO(aluint, frequency, "frequency in Hz");
    SET_ARGINFO(alcenum, format, "audio data format");
    SET_ARGINFO(sizei, buffersize, "buffer size in sample frames (not bytes!)");
    SET_RETINFO(device);

    if (!retval) {
        info->reported_failure = AL_TRUE;
    } else {
        StateTrie *trie = visitargs->frame->getStateTrie();
        trie->addDeviceStateRevision(retval, "opened", 1);
        trie->addDeviceStateRevision(retval, "devtype", 1);
        trie->addDeviceStateRevision(retval, "openname", (uint64) devicename);
        trie->addDeviceStateRevision(retval, "frequency", (uint64) frequency);
        trie->addDeviceStateRevision(retval, "format", (uint64) format);
        trie->addDeviceStateRevision(retval, "buffersize", (uint64) buffersize);
        trie->addDeviceStateRevision(retval, "capturing", 0);
        trie->addDeviceStateRevision(retval, "ALC_MAJOR_VERSION", (uint64) major_version);
        trie->addDeviceStateRevision(retval, "ALC_MINOR_VERSION", (uint64) minor_version);
        trie->addDeviceStateRevision(retval, "ALC_CAPTURE_DEVICE_SPECIFIER", (uint64) ((size_t) devspec));
        trie->addDeviceStateRevision(retval, "ALC_EXTENSIONS", (uint64) ((size_t) extensions));

        const uint64 *val = trie->getGlobalState("numdevices");
        uint64 numdevs = (val ? *val : 0);
        trie->addGlobalStateRevision("numdevices", numdevs + 1);
        char buf[64];
        snprintf(buf, sizeof (buf), "device/%u", (uint) numdevs);
        trie->addGlobalStateRevision(buf, (uint64) retval);
    }
}

static void make_state_alcCaptureCloseDevice(CallerInfo *callerinfo, ALCboolean retval, ALCdevice *device)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to close");
    SET_RETINFO(alcbool);

    if (!retval) {
        info->reported_failure = AL_TRUE;
    } else {
        StateTrie *trie = visitargs->frame->getStateTrie();
        trie->addDeviceStateRevision(device, "opened", 0);
        const uint64 *val = trie->getGlobalState("numdevices");
        uint64 numdevs = (val ? *val : 0);
        for (uint64 i = 0; i < numdevs; i++) {
            char buf[64];
            snprintf(buf, sizeof (buf), "device/%u", (uint) i);
            val = trie->getGlobalState(buf);
            if (val && (*val == ((uint64) device))) {
                trie->addGlobalStateRevision(buf, 0);
                break;
            }
        }
    }
}

static void make_state_alcOpenDevice(CallerInfo *callerinfo, ALCdevice *retval, const ALCchar *devicename, ALint major_version, ALint minor_version, const ALCchar *devspec, const ALCchar *extensions)
{
    devicename = cache_string(devicename);
    devspec = cache_string(devspec);
    extensions = cache_string(extensions);
    START_ARGS();
    SET_ARGINFO(string, devicename, "device name to open");
    SET_RETINFO(device);

    if (!retval) {
        info->reported_failure = AL_TRUE;
    } else {
        StateTrie *trie = visitargs->frame->getStateTrie();
        trie->addDeviceStateRevision(retval, "opened", 1);
        trie->addDeviceStateRevision(retval, "devtype", 0);
        trie->addDeviceStateRevision(retval, "openname", (uint64) devicename);
        trie->addDeviceStateRevision(retval, "ALC_MAJOR_VERSION", (uint64) major_version);
        trie->addDeviceStateRevision(retval, "ALC_MINOR_VERSION", (uint64) minor_version);
        trie->addDeviceStateRevision(retval, "ALC_DEVICE_SPECIFIER", (uint64) ((size_t) devspec));
        trie->addDeviceStateRevision(retval, "ALC_EXTENSIONS", (uint64) ((size_t) extensions));

        const uint64 *val = trie->getGlobalState("numdevices");
        uint64 numdevs = (val ? *val : 0);
        trie->addGlobalStateRevision("numdevices", numdevs + 1);
        char buf[64];
        snprintf(buf, sizeof (buf), "device/%u", (uint) numdevs);
        trie->addGlobalStateRevision(buf, (uint64) retval);
    }
}

static void make_state_alcCloseDevice(CallerInfo *callerinfo, ALCboolean retval, ALCdevice *device)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to close");
    SET_RETINFO(alcbool);
    if (!retval) {
        info->reported_failure = AL_TRUE;
    } else {
        StateTrie *trie = visitargs->frame->getStateTrie();
        trie->addDeviceStateRevision(device, "opened", 0);
        // We don't shrink this array, we just zero out elements.
        const uint64 *val = trie->getGlobalState("numdevices");
        uint64 numdevs = (val ? *val : 0);
        for (uint64 i = 0; i < numdevs; i++) {
            char buf[64];
            snprintf(buf, sizeof (buf), "device/%u", (uint) i);
            val = trie->getGlobalState(buf);
            if (val && (*val == ((uint64) device))) {
                trie->addGlobalStateRevision(buf, 0);
                break;
            }
        }
    }
}

static void make_state_alcCreateContext(CallerInfo *callerinfo, ALCcontext *retval, ALCdevice *device, const ALCint *origattrlist, uint32 attrcount, const ALCint *attrlist)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to assign context");
    SET_ARGINFO(ptr, origattrlist, "attribute list");
    SET_RETINFO(context);
    if (!retval) {
        info->reported_failure = AL_TRUE;
    } else {
        StateTrie *trie = visitargs->frame->getStateTrie();
        trie->addContextStateRevision(retval, "processing", 1);
        trie->addContextStateRevision(retval, "created", 1);
        trie->addContextStateRevision(retval, "device", (uint64) device);

        // !!! FIXME: can these change? Should we query for them during recording?
        trie->addContextStateRevision(retval, "ALC_ATTRIBUTES_SIZE", (uint64) attrcount);
        for (uint32 i = 0; i < attrcount; i++) {
            char buf[64];
            snprintf(buf, sizeof (buf), "ALC_ALL_ATTRIBUTES/%u", (uint) i);
            union { ALCint i; uint64 ui64; } cvt; cvt.i = attrlist[i];
            trie->addContextStateRevision(retval, buf, cvt.ui64);
        }

        const uint64 *val = trie->getDeviceState(device, "numcontexts");
        uint64 numctxs = (val ? *val : 0);
        trie->addDeviceStateRevision(device, "numcontexts", numctxs + 1);
        char buf[64];
        snprintf(buf, sizeof (buf), "context/%u", (uint) numctxs);
        trie->addDeviceStateRevision(device, buf, (uint64) retval);
    }
}

static void make_state_alcMakeContextCurrent(CallerInfo *callerinfo, ALCboolean retval, ALCcontext *ctx)
{
    START_ARGS();
    SET_ARGINFO(context, ctx, "context to make current");
    SET_RETINFO(alcbool);

    if (!retval) {
        info->reported_failure = AL_TRUE;
    } else {
        StateTrie *trie = visitargs->frame->getStateTrie();
        if (ctx == trie->getCurrentContext()) {
            info->inefficient_state_change = AL_TRUE;
        } else {
            trie->setCurrentContext(ctx);
        }
    }
}

static void make_state_alcProcessContext(CallerInfo *callerinfo, ALCcontext *ctx)
{
    START_ARGS();
    SET_ARGINFO(context, ctx, "context to begin processing");

    StateTrie *trie = visitargs->frame->getStateTrie();
    const uint64 *val = trie->getContextState(ctx, "processing");
    if (val && *val) {
        info->inefficient_state_change = AL_TRUE;
    } else {
        trie->addContextStateRevision(ctx, "processing", 1);
    }
}

static void make_state_alcSuspendContext(CallerInfo *callerinfo, ALCcontext *ctx)
{
    START_ARGS();
    SET_ARGINFO(context, ctx, "context to suspend processing");

    StateTrie *trie = visitargs->frame->getStateTrie();
    const uint64 *val = trie->getContextState(ctx, "processing");
    if (val && *val) {
        trie->addContextStateRevision(ctx, "processing", 0);
    } else {
        info->inefficient_state_change = AL_TRUE;
    }
}

static void make_state_alcDestroyContext(CallerInfo *callerinfo, ALCcontext *ctx)
{
    START_ARGS();
    SET_ARGINFO(context, ctx, "context to destroy");

    if (true) {  // !!! FIXME: don't mark deleted if there's an error triggered in alcDestroyContext...
        StateTrie *trie = visitargs->frame->getStateTrie();
        trie->addContextStateRevision(ctx, "created", 0);
        const uint64 *val = trie->getContextState(ctx, "device");
        if (val) {
            ALCdevice *device = (ALCdevice *) ((size_t) *val);
            // We don't shrink this array, we just zero out elements.
            val = trie->getDeviceState(device, "numcontexts");
            uint64 numctxs = (val ? *val : 0);
            for (uint64 i = 0; i < numctxs; i++) {
                char buf[64];
                snprintf(buf, sizeof (buf), "context/%u", (uint) i);
                val = trie->getDeviceState(device, buf);
                if (val && (*val == ((uint64) ctx))) {
                    trie->addDeviceStateRevision(device, buf, 0);
                    break;
                }
            }
        }
    }
}

static void make_state_alcGetError(CallerInfo *callerinfo, ALCenum retval, ALCdevice *device)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to query");
    SET_RETINFO(alcenum);

    if (device) {
        StateTrie *trie = visitargs->frame->getStateTrie();
        const uint64 *val = trie->getDeviceState(device, "error");
        if (!val || (*val == ALC_NO_ERROR)) {
            info->inefficient_state_change = AL_TRUE;
        } else {
            trie->addDeviceStateRevision(device, "error", (uint64) ALC_NO_ERROR);
        }

    }
}

static void make_state_alcGetIntegerv(CallerInfo *callerinfo, ALCdevice *device, ALCenum param, ALCsizei size, ALCint *origvalues, ALCboolean isbool, ALCint *values)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to query");
    SET_ARGINFO(alcenum, param, "parameter");
    SET_ARGINFO(sizei, size, "size of buffer (in ALCints, not bytes!)");
    SET_ARGINFO(ptr, origvalues, "buffer for obtained values");
    SET_RETINFOCOUNT(size);
    for (ALCsizei i = 0; i < size; i++) {
        if (isbool) {
            SET_RETINFOn(i, alcbool, (ALCboolean) values[i]);
        } else {
            SET_RETINFOn(i, alint, values[i]);
        }
    }
}

static void make_state_alcCaptureStart(CallerInfo *callerinfo, ALCdevice *device)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to start capturing from");

    StateTrie *trie = visitargs->frame->getStateTrie();
    const uint64 *val = trie->getDeviceState(device, "devtype");
    if (val && (*val == 1)) {
        val = trie->getDeviceState(device, "capturing");
        if (val && *val) {
            info->inefficient_state_change = AL_TRUE;
        } else {
            trie->addDeviceStateRevision(device, "capturing", 1);
        }
    } else {
        info->reported_failure = AL_TRUE;
    }
}

static void make_state_alcCaptureStop(CallerInfo *callerinfo, ALCdevice *device)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to stop capturing from");

    StateTrie *trie = visitargs->frame->getStateTrie();
    const uint64 *val = trie->getDeviceState(device, "devtype");
    if (val && (*val == 1)) {
        val = trie->getDeviceState(device, "capturing");
        if (val && *val) {
            trie->addDeviceStateRevision(device, "capturing", 0);
        } else {
            info->inefficient_state_change = AL_TRUE;
        }
    } else {
        info->reported_failure = AL_TRUE;
    }
}

static void make_state_alcCaptureSamples(CallerInfo *callerinfo, ALCdevice *device, ALCvoid *origbuffer, ALCvoid *buffer, ALCsizei bufferlen, ALCsizei samples)
{
    START_ARGS();
    SET_ARGINFO(device, device, "device to capture from");
    SET_ARGINFO(ptr, origbuffer, "buffer to fill with samples");
    SET_ARGINFO(sizei, samples, "size of buffer in samples (not bytes!)");

    StateTrie *trie = visitargs->frame->getStateTrie();
    const uint64 *val = trie->getDeviceState(device, "devtype");
    if (val && (*val == 1)) {
        val = trie->getDeviceState(device, "capturing");
        if (!val || !*val || !origbuffer) {
            info->reported_failure = AL_TRUE;
        } else if (samples == 0) {
            info->inefficient_state_change = AL_TRUE;
        } else {
            // !!! FIXME: this needs to decide if data was actually available from the AL.
            char buf[64];
            val = trie->getDeviceState(device, "numcaptures");
            const uint64 numcaptures = val ? *val : 0;
            snprintf(buf, sizeof (buf), "capturedatalen/%u", (uint) numcaptures);
            trie->addDeviceStateRevision(device, buf, (uint64) bufferlen);
            snprintf(buf, sizeof (buf), "capturedata/%u", (uint) numcaptures);
            trie->addDeviceStateRevision(device, buf, (uint64) (callerinfo->fdoffset + 32));
            trie->addDeviceStateRevision(device, "numcaptures", numcaptures + 1);
        }
    } else {
        info->reported_failure = AL_TRUE;
    }
}

static void make_state_alDopplerFactor(CallerInfo *callerinfo, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(alfloat, value, "new doppler factor");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alDopplerVelocity(CallerInfo *callerinfo, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(alfloat, value, "new doppler velocity");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSpeedOfSound(CallerInfo *callerinfo, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(alfloat, value, "new speed of sound");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alDistanceModel(CallerInfo *callerinfo, ALenum model)
{
    START_ARGS();
    SET_ARGINFO(alenum, model, "new distance model");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alEnable(CallerInfo *callerinfo, ALenum capability)
{
    START_ARGS();
    SET_ARGINFO(alenum, capability, "capability to enable");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alDisable(CallerInfo *callerinfo, ALenum capability)
{
    START_ARGS();
    SET_ARGINFO(alenum, capability, "capability to disable");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alIsEnabled(CallerInfo *callerinfo, ALboolean retval, ALenum capability)
{
    START_ARGS();
    SET_ARGINFO(alenum, capability, "capability");
    SET_RETINFO(albool);
}

static void make_state_alGetString(CallerInfo *callerinfo, const ALchar *retval, const ALenum param)
{
    retval = cache_string(retval);
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_RETINFO(string);
}

static void make_state_alGetBooleanv(CallerInfo *callerinfo, ALenum param, ALboolean *origvalues, uint32 numvals, ALboolean *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "buffer for obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, albool, values[i]);
    }
}

static void make_state_alGetIntegerv(CallerInfo *callerinfo, ALenum param, ALint *origvalues, uint32 numvals, ALboolean isenum, ALint *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "buffer for obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        if (isenum) {
            SET_RETINFOn(i, alenum, (ALenum) values[i]);
        } else {
            SET_RETINFOn(i, alint, values[i]);
        }
    }
}

static void make_state_alGetFloatv(CallerInfo *callerinfo, ALenum param, ALfloat *origvalues, uint32 numvals, ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "buffer for obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, alfloat, values[i]);
    }
}

static void make_state_alGetDoublev(CallerInfo *callerinfo, ALenum param, ALdouble *origvalues, uint32 numvals, ALdouble *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "buffer for obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, aldouble, values[i]);
    }
}

static void make_state_alGetBoolean(CallerInfo *callerinfo, ALboolean retval, ALenum param)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_RETINFO(albool);
}

static void make_state_alGetInteger(CallerInfo *callerinfo, ALint retval, ALenum param)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    if (param == AL_DISTANCE_MODEL) {
        SET_RETINFO(alenum);
    } else {
        SET_RETINFO(alint);
    }
}

static void make_state_alGetFloat(CallerInfo *callerinfo, ALfloat retval, ALenum param)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_RETINFO(alfloat);
}

static void make_state_alGetDouble(CallerInfo *callerinfo, ALdouble retval, ALenum param)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_RETINFO(aldouble);
}

static void make_state_alIsExtensionPresent(CallerInfo *callerinfo, ALboolean retval, const ALchar *extname)
{
    extname = cache_string(extname);
    START_ARGS();
    SET_ARGINFO(string, extname, "extension name");
    SET_RETINFO(albool);
}

static void make_state_alGetError(CallerInfo *callerinfo, ALenum retval)
{
    START_ARGS();
    SET_RETINFO(alenum);

    ALCcontext *ctx = visitargs->frame->getStateTrie()->getCurrentContext();
    if (ctx) {
        StateTrie *trie = visitargs->frame->getStateTrie();
        const uint64 *val = trie->getContextState(ctx, "error");
        if (!val || (*val == AL_NO_ERROR)) {
            info->inefficient_state_change = AL_TRUE;
        } else {
            trie->addContextStateRevision(ctx, "error", (uint64) AL_NO_ERROR);
        }
    }
}

static void make_state_alGetProcAddress(CallerInfo *callerinfo, void *retval, const ALchar *funcname)
{
    funcname = cache_string(funcname);
    START_ARGS();
    SET_ARGINFO(string, funcname, "function name");
    SET_RETINFO(ptr);
    if (!retval) {
        info->reported_failure = AL_TRUE;
    }
}

static void make_state_alGetEnumValue(CallerInfo *callerinfo, ALenum retval, const ALchar *enumname)
{
    enumname = cache_string(enumname);
    START_ARGS();
    SET_ARGINFO(string, enumname, "enum name");
    SET_RETINFO(alenum);
}

static void make_state_alListenerfv(CallerInfo *callerinfo, ALenum param, const ALfloat *origvalues, uint32 numvals, const ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "buffer of new values");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alListenerf(CallerInfo *callerinfo, ALenum param, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alfloat, value, "new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alListener3f(CallerInfo *callerinfo, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    if ((param == AL_POSITION) || (param == AL_VELOCITY)) {  // !!! FIXME: we need to fill in more of these.
        SET_ARGINFO(alfloat, value1, "X");
        SET_ARGINFO(alfloat, value2, "Y");
        SET_ARGINFO(alfloat, value3, "Z");
    } else {
        SET_ARGINFO(alfloat, value1, "first new value");
        SET_ARGINFO(alfloat, value2, "second new value");
        SET_ARGINFO(alfloat, value3, "third new value");
    }
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alListeneriv(CallerInfo *callerinfo, ALenum param, const ALint *origvalues, uint32 numvals, const ALint *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to new values");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alListeneri(CallerInfo *callerinfo, ALenum param, ALint value)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alint, value, "new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alListener3i(CallerInfo *callerinfo, ALenum param, ALint value1, ALint value2, ALint value3)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alint, value1, "first new value");
    SET_ARGINFO(alint, value2, "second new value");
    SET_ARGINFO(alint, value3, "third new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alGetListenerfv(CallerInfo *callerinfo, ALenum param, ALfloat *origvalues, uint32 numvals, ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, alfloat, values[i]);
    }
}

static void make_state_alGetListenerf(CallerInfo *callerinfo, ALenum param, ALfloat *origvalue, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue, "pointer to obtained value");
    SET_RETINFOCOUNT(1);
    SET_RETINFOn(0, alfloat, value);
}

static void make_state_alGetListener3f(CallerInfo *callerinfo, ALenum param, ALfloat *origvalue1, ALfloat *origvalue2, ALfloat *origvalue3, ALfloat value1, ALfloat value2, ALfloat value3)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue1, "pointer for first obtained value");
    SET_ARGINFO(ptr, origvalue2, "pointer for second obtained value");
    SET_ARGINFO(ptr, origvalue3, "pointer for third obtained value");
    SET_RETINFOCOUNT(3);
    SET_RETINFOn(0, alfloat, value1);
    SET_RETINFOn(1, alfloat, value2);
    SET_RETINFOn(2, alfloat, value3);
}

static void make_state_alGetListeneri(CallerInfo *callerinfo, ALenum param, ALint *origvalue, ALint value)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue, "pointer to obtained value");
    SET_RETINFOCOUNT(1);
    SET_RETINFOn(0, alint, value);
}

static void make_state_alGetListeneriv(CallerInfo *callerinfo, ALenum param, ALint *origvalues, uint32 numvals, ALint *values)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, alint, values[i]);
    }
}

static void make_state_alGetListener3i(CallerInfo *callerinfo, ALenum param, ALint *origvalue1, ALint *origvalue2, ALint *origvalue3, ALint value1, ALint value2, ALint value3)
{
    START_ARGS();
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue1, "pointer to first obtained value");
    SET_ARGINFO(ptr, origvalue2, "pointer to second obtained value");
    SET_ARGINFO(ptr, origvalue3, "pointer to third obtained value");
    SET_RETINFOCOUNT(3);
    SET_RETINFOn(0, alint, value1);
    SET_RETINFOn(1, alint, value2);
    SET_RETINFOn(2, alint, value3);
}

static void make_state_alGenSources(CallerInfo *callerinfo, ALsizei n, ALuint *orignames, ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of sources to generate");
    SET_ARGINFO(ptr, orignames, "pointer to obtained source names");

    // !!! FIXME: store these as ranges of sources to save memory and lookups at some point.
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        ALsizei total = 0;
        const uint64 *val = trie->getContextState(ctx, "numsources");
        const uint64 numsrcs = val ? *val : 0;
        for (ALsizei i = 0; i < n; i++) {
            const ALuint name = names[i];
            if (name) {
                char buf[64];
                snprintf(buf, sizeof (buf), "source/%u", (uint) (numsrcs + total));
                trie->addContextStateRevision(ctx, buf, (uint64) name);
                trie->addSourceStateRevision(ctx, name, "allocated", 1);
                total++;
            }
        }
        trie->addContextStateRevision(ctx, "numsources", (uint64) (numsrcs + total));

        SET_RETINFOCOUNT(n);
        for (ALsizei i = 0; i < n; i++) {
            SET_RETINFOn(i, source, names[i]);
        }
    }
}

static void make_state_alDeleteSources(CallerInfo *callerinfo, ALsizei n, const ALuint *orignames, const ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of sources to delete");
    SET_ARGINFO(ptr, orignames, "array of source names");

    // !!! FIXME: store these as ranges of sources to save memory and lookups at some point.
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        for (ALsizei i = 0; i < n; i++) {
            const ALuint name = names[i];
            if (name) {
                trie->addSourceStateRevision(ctx, name, "allocated", 0);
            }
        }
    }
}

static void make_state_alIsSource(CallerInfo *callerinfo, ALboolean retval, ALuint name)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_RETINFO(albool);
}

static void make_state_alSourcefv(CallerInfo *callerinfo, ALuint name, ALenum param, const ALfloat *origvalues, uint32 numvals, const ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to new values");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourcef(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alfloat, value, "new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSource3f(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    if ((param == AL_POSITION) || (param == AL_VELOCITY)) {  // !!! FIXME: we need to fill in more of these.
        SET_ARGINFO(alfloat, value1, "X");
        SET_ARGINFO(alfloat, value2, "Y");
        SET_ARGINFO(alfloat, value3, "Z");
    } else {
        SET_ARGINFO(alfloat, value1, "first new value");
        SET_ARGINFO(alfloat, value2, "second new value");
        SET_ARGINFO(alfloat, value3, "third new value");
    }
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceiv(CallerInfo *callerinfo, ALuint name, ALenum param, const ALint *origvalues, uint32 numvals, const ALint *values)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to new values");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourcei(CallerInfo *callerinfo, ALuint name, ALenum param, ALint value)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");

    if (param == AL_BUFFER) {
        SET_ARGINFO(buffer, value, "new value");
    } else if (param == AL_LOOPING) {
        SET_ARGINFO(albool, value, "new value");
    } else if (param == AL_SOURCE_RELATIVE) {
        SET_ARGINFO(albool, value, "new value");
    } else if (param == AL_SOURCE_TYPE) {
        SET_ARGINFO(alenum, value, "new value");
    } else if (param == AL_SOURCE_STATE) {
        SET_ARGINFO(alenum, value, "new value");
    } else {
        SET_ARGINFO(alint, value, "new value");
    }
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSource3i(CallerInfo *callerinfo, ALuint name, ALenum param, ALint value1, ALint value2, ALint value3)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alint, value1, "first new value");
    SET_ARGINFO(alint, value2, "second new value");
    SET_ARGINFO(alint, value3, "third new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alGetSourcefv(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat *origvalues, uint32 numvals, ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, alfloat, values[i]);
    }
}

static void make_state_alGetSourcef(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat *origvalue, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue, "pointer to obtained value");
    SET_RETINFOCOUNT(1);
    SET_RETINFOn(0, alfloat, value);
}

static void make_state_alGetSource3f(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat *origvalue1, ALfloat *origvalue2, ALfloat *origvalue3, ALfloat value1, ALfloat value2, ALfloat value3)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue1, "pointer for first obtained value");
    SET_ARGINFO(ptr, origvalue2, "pointer for second obtained value");
    SET_ARGINFO(ptr, origvalue3, "pointer for third obtained value");
    SET_RETINFOCOUNT(3);
    SET_RETINFOn(0, alfloat, value1);
    SET_RETINFOn(1, alfloat, value2);
    SET_RETINFOn(2, alfloat, value3);
}

static void make_state_alGetSourceiv(CallerInfo *callerinfo, ALuint name, ALenum param, ALboolean isenum, ALint *origvalues, uint32 numvals, ALint *values)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        if (isenum) {
            SET_RETINFOn(i, alenum, (ALenum) values[i]);
        } else {
            SET_RETINFOn(i, alint, values[i]);
        }
    }
}

static void make_state_alGetSourcei(CallerInfo *callerinfo, ALuint name, ALenum param, ALboolean isenum, ALint *origvalue, ALint value)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue, "pointer to obtained value");
    SET_RETINFOCOUNT(1);
    if (isenum) {
        SET_RETINFOn(0, alenum, (ALenum) value);
    } else {
        SET_RETINFOn(0, alint, value);
    }
}

static void make_state_alGetSource3i(CallerInfo *callerinfo, ALuint name, ALenum param, ALint *origvalue1, ALint *origvalue2, ALint *origvalue3, ALint value1, ALint value2, ALint value3)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue1, "pointer for first obtained value");
    SET_ARGINFO(ptr, origvalue2, "pointer for second obtained value");
    SET_ARGINFO(ptr, origvalue3, "pointer for third obtained value");
    SET_RETINFOCOUNT(3);
    SET_RETINFOn(0, alint, value1);
    SET_RETINFOn(1, alint, value2);
    SET_RETINFOn(2, alint, value3);
}

static void make_state_alSourcePlay(CallerInfo *callerinfo, ALuint name)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source to play");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourcePlayv(CallerInfo *callerinfo, ALsizei n, const ALuint *orignames, const ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of sources to play");
    SET_ARGINFO(ptr, orignames, "array of source names");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourcePause(CallerInfo *callerinfo, ALuint name)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source to pause");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourcePausev(CallerInfo *callerinfo, ALsizei n, const ALuint *orignames, const ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of sources to pause");
    SET_ARGINFO(ptr, orignames, "array of source names");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceRewind(CallerInfo *callerinfo, ALuint name)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source to rewind");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceRewindv(CallerInfo *callerinfo, ALsizei n, const ALuint *orignames, const ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of sources to rewind");
    SET_ARGINFO(ptr, orignames, "array of source names");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceStop(CallerInfo *callerinfo, ALuint name)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source to stop");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceStopv(CallerInfo *callerinfo, ALsizei n, const ALuint *orignames, const ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of sources to stop");
    SET_ARGINFO(ptr, orignames, "array of source names");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceQueueBuffers(CallerInfo *callerinfo, ALuint name, ALsizei nb, const ALuint *origbufnames, const ALuint *bufnames)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(sizei, nb, "number of buffers to queue");
    SET_ARGINFO(ptr, origbufnames, "array of buffer names");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alSourceUnqueueBuffers(CallerInfo *callerinfo, ALuint name, ALsizei nb, ALuint *origbufnames, ALuint *bufnames)
{
    START_ARGS();
    SET_ARGINFO(source, name, "source");
    SET_ARGINFO(sizei, nb, "number of buffers to unqueue");
    SET_ARGINFO(ptr, origbufnames, "pointer to unqueued buffer names");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.

    SET_RETINFOCOUNT(nb);
    for (ALsizei i = 0; i < nb; i++) {
        SET_RETINFOn(i, buffer, bufnames[i]);
    }
}

static void make_state_alGenBuffers(CallerInfo *callerinfo, ALsizei n, ALuint *orignames, ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of buffers to generate");
    SET_ARGINFO(ptr, orignames, "pointer to obtained buffer names");

    // !!! FIXME: store these as ranges of buffers to save memory and lookups at some point.
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCdevice *dev = NULL;
    ALCcontext *ctx = trie->getCurrentContext(&dev);
    if (ctx && dev) {
        ALsizei total = 0;
        const uint64 *val = trie->getDeviceState(dev, "numbuffers");
        const uint64 numbufs = val ? *val : 0;
        for (ALsizei i = 0; i < n; i++) {
            const ALuint name = names[i];
            if (name) {
                char buf[64];
                snprintf(buf, sizeof (buf), "buffer/%u", (uint) (numbufs + total));
                trie->addDeviceStateRevision(dev, buf, (uint64) name);
                trie->addBufferStateRevision(dev, name, "allocated", 1);
                total++;
            }
        }
        trie->addDeviceStateRevision(dev, "numbuffers", (uint64) (numbufs + total));

        SET_RETINFOCOUNT(n);
        for (ALsizei i = 0; i < n; i++) {
            SET_RETINFOn(i, buffer, names[i]);
        }
    }
}

static void make_state_alDeleteBuffers(CallerInfo *callerinfo, ALsizei n, const ALuint *orignames, const ALuint *names)
{
    START_ARGS();
    SET_ARGINFO(sizei, n, "number of buffers to delete");
    SET_ARGINFO(ptr, orignames, "array of buffer names");

    // !!! FIXME: store these as ranges of sources to save memory and lookups at some point.
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCdevice *dev = NULL;
    ALCcontext *ctx = trie->getCurrentContext(&dev);
    if (ctx && dev) {
        for (ALsizei i = 0; i < n; i++) {
            const ALuint name = names[i];
            if (name) {
                trie->addBufferStateRevision(dev, name, "allocated", 0);
            }
        }
    }
}

static void make_state_alIsBuffer(CallerInfo *callerinfo, ALboolean retval, ALuint name)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_RETINFO(albool);
}

static void make_state_alBufferData(CallerInfo *callerinfo, ALuint name, ALenum alfmt, const ALvoid *origdata, const ALvoid *data, ALsizei size, ALsizei freq)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, alfmt, "audio data format");
    SET_ARGINFO(ptr, origdata, "buffer of audio data");
    SET_ARGINFO(sizei, size, "size of buffer in bytes (not samples!)");
    SET_ARGINFO(sizei, freq, "frequency of audio data in Hz");
    // !!! FIXME: compare existing data and mark as an inefficient state change if the data is identical.

    if (name) {
        StateTrie *trie = visitargs->frame->getStateTrie();
        ALCdevice *dev = NULL;
        ALCcontext *ctx = trie->getCurrentContext(&dev);
        if (ctx && dev) {
            trie->addBufferStateRevision(dev, name, "format", (uint64) alfmt);
            trie->addBufferStateRevision(dev, name, "data", (uint64) (origdata ? (callerinfo->fdoffset + 32) : 0));
            trie->addBufferStateRevision(dev, name, "datalen", (uint64) (origdata ? size : 0));
        }
    }
}

static void make_state_alBufferfv(CallerInfo *callerinfo, ALuint name, ALenum param, const ALfloat *origvalues, uint32 numvals, const ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to new values");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alBufferf(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alfloat, value, "new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alBuffer3f(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alfloat, value1, "first new value");
    SET_ARGINFO(alfloat, value2, "second new value");
    SET_ARGINFO(alfloat, value3, "third new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alBufferiv(CallerInfo *callerinfo, ALuint name, ALenum param, const ALint *origvalues, uint32 numvals, const ALint *values)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to new values");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alBufferi(CallerInfo *callerinfo, ALuint name, ALenum param, ALint value)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alint, value, "new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alBuffer3i(CallerInfo *callerinfo, ALuint name, ALenum param, ALint value1, ALint value2, ALint value3)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(alint, value1, "first new value");
    SET_ARGINFO(alint, value2, "second new value");
    SET_ARGINFO(alint, value3, "third new value");
    info->inefficient_state_change = AL_TRUE;  // will reset to AL_FALSE if we get a state change event.
}

static void make_state_alGetBufferfv(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat *origvalues, uint32 numvals, ALfloat *values)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, alfloat, values[i]);
    }
}

static void make_state_alGetBufferf(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat *origvalue, ALfloat value)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue, "pointer to obtained value");
    SET_RETINFOCOUNT(1);
    SET_RETINFOn(0, alfloat, value);
}

static void make_state_alGetBuffer3f(CallerInfo *callerinfo, ALuint name, ALenum param, ALfloat *origvalue1, ALfloat *origvalue2, ALfloat *origvalue3, ALfloat value1, ALfloat value2, ALfloat value3)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue1, "pointer for first obtained value");
    SET_ARGINFO(ptr, origvalue2, "pointer for second obtained value");
    SET_ARGINFO(ptr, origvalue3, "pointer for third obtained value");
    SET_RETINFOCOUNT(3);
    SET_RETINFOn(0, alfloat, value1);
    SET_RETINFOn(1, alfloat, value2);
    SET_RETINFOn(2, alfloat, value3);
}

static void make_state_alGetBufferi(CallerInfo *callerinfo, ALuint name, ALenum param, ALint *origvalue, ALint value)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue, "pointer to obtained value");
    SET_RETINFOCOUNT(1);
    SET_RETINFOn(0, alint, value);
}

static void make_state_alGetBuffer3i(CallerInfo *callerinfo, ALuint name, ALenum param, ALint *origvalue1, ALint *origvalue2, ALint *origvalue3, ALint value1, ALint value2, ALint value3)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalue1, "pointer for first obtained value");
    SET_ARGINFO(ptr, origvalue2, "pointer for second obtained value");
    SET_ARGINFO(ptr, origvalue3, "pointer for third obtained value");
    SET_RETINFOCOUNT(3);
    SET_RETINFOn(0, alint, value1);
    SET_RETINFOn(1, alint, value2);
    SET_RETINFOn(2, alint, value3);
}

static void make_state_alGetBufferiv(CallerInfo *callerinfo, ALuint name, ALenum param, ALint *origvalues, uint32 numvals, ALint *values)
{
    START_ARGS();
    SET_ARGINFO(buffer, name, "buffer");
    SET_ARGINFO(alenum, param, "parameter");
    SET_ARGINFO(ptr, origvalues, "pointer to obtained values");
    SET_RETINFOCOUNT(numvals);
    for (uint32 i = 0; i < numvals; i++) {
        SET_RETINFOn(i, alint, values[i]);
    }
}

static void make_state_alTracePushScope(CallerInfo *callerinfo, const ALchar *str)
{
    str = cache_string(str);
    START_ARGS();
    SET_ARGINFO(string, str, "new scope's name");
}

static void make_state_alTracePopScope(CallerInfo *callerinfo)
{
    START_ARGS();
}

static void make_state_alTraceMessage(CallerInfo *callerinfo, const ALchar *str)
{
    str = cache_string(str);
    START_ARGS();
    SET_ARGINFO(string, str, "message string");
}

static void make_state_alTraceBufferLabel(CallerInfo *callerinfo, ALuint name, const ALchar *str)
{
    str = cache_string(str);
    START_ARGS();
    SET_ARGINFO(aluint, name, "buffer");  // this is intensionally listed as aluint instead of buffer, so old name isn't listed in output.
    SET_ARGINFO(string, str, "new label");

    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCdevice *dev = NULL;
    ALCcontext *ctx = trie->getCurrentContext(&dev);
    if (ctx && dev) {
        trie->addBufferStateRevision(dev, name, "label", (uint64) ((size_t) str));
    }
}

static void make_state_alTraceSourceLabel(CallerInfo *callerinfo, ALuint name, const ALchar *str)
{
    str = cache_string(str);
    START_ARGS();
    SET_ARGINFO(aluint, name, "source");  // this is intensionally listed as aluint instead of source, so old name isn't listed in output.
    SET_ARGINFO(string, str, "new label");

    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        trie->addSourceStateRevision(ctx, name, "label", (uint64) ((size_t) str));
    }
}

static void make_state_alcTraceDeviceLabel(CallerInfo *callerinfo, ALCdevice *device, const ALCchar *str)
{
    str = cache_string(str);
    START_ARGS();
    SET_ARGINFO(ptr, device, "device"); // this is intensionally listed as ptr instead of device, so old name isn't listed in output.
    SET_ARGINFO(string, str, "new label");

    StateTrie *trie = visitargs->frame->getStateTrie();
    trie->addDeviceStateRevision(device, "label", (uint64) ((size_t) str));
}

static void make_state_alcTraceContextLabel(CallerInfo *callerinfo, ALCcontext *ctx, const ALCchar *str)
{
    str = cache_string(str);
    START_ARGS();
    SET_ARGINFO(ptr, ctx, "context"); // this is intensionally listed as ptr instead of context, so old name isn't listed in output.
    SET_ARGINFO(string, str, "new label");

    StateTrie *trie = visitargs->frame->getStateTrie();
    trie->addContextStateRevision(ctx, "label", (uint64) ((size_t) str));
}


#define ENTRYPOINT(ret,name,params,args,numargs,visitparams,visitargs) \
    void visit_##name visitparams { \
        VisitArgs *vargs = (VisitArgs *) callerinfo->userdata; \
        ALTraceFrame *frame = vargs->frame; \
        ApiCallInfo *info = frame->getApiCallGridTable()->getApiCallInfo(); \
        if (info) { info->state = frame->getStateTrie()->snapshotState();  /* lock down state for previous call. */ } \
        info = new ApiCallInfo(#name, ALEE_##name, numargs, callerinfo); \
        vargs->info = info; \
        make_state_##name visitargs; \
        frame->getApiCallGridTable()->appendApiCall(info, callerinfo); \
        const int w = strlen(info->callstr); \
        if (vargs->longestcallstr_width < w) { \
            vargs->longestcallstr = info->callstr; \
            vargs->longestcallstr_width = w; \
        } \
    }
#include "altrace_entrypoints.h"


static void mark_visit_as_changed_state(VisitArgs *visitargs)
{
    visitargs->info->inefficient_state_change = AL_FALSE;
}

void visit_al_error_event(void *userdata, const ALenum err)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    visitargs->info->reported_failure = AL_TRUE;
    visitargs->info->generated_al_error = err;
    ALCcontext *ctx = visitargs->frame->getStateTrie()->getCurrentContext();
    if (ctx) {
        visitargs->frame->getStateTrie()->addContextStateRevision(ctx, "error", (uint64) err);
    }
}

void visit_alc_error_event(void *userdata, ALCdevice *device, const ALCenum err)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    visitargs->info->reported_failure = AL_TRUE;
    visitargs->info->generated_alc_error = err;
    if (device) {
        visitargs->frame->getStateTrie()->addDeviceStateRevision(device, "error", (uint64) err);
    }
}

void visit_device_state_changed_int(void *userdata, ALCdevice *dev, const ALCenum param, const ALCint newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    if (param != ALC_CAPTURE_SAMPLES) {  /* these don't progress because of API calls */
        mark_visit_as_changed_state(visitargs);
    }
    StateTrie *trie = visitargs->frame->getStateTrie();
    union { ALCint i; uint64 ui64; } cvt; cvt.i = newval;
    trie->addDeviceStateRevision(dev, alcenumString(param), cvt.ui64);
}

void visit_context_state_changed_enum(void *userdata, ALCcontext *ctx, const ALenum param, const ALenum newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    StateTrie *trie = visitargs->frame->getStateTrie();
    trie->addContextStateRevision(ctx, alenumString(param), (uint64) newval);
}

void visit_context_state_changed_float(void *userdata, ALCcontext *ctx, const ALenum param, const ALfloat newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    union { ALfloat f; uint64 ui64; } cvt; cvt.f = newval;
    StateTrie *trie = visitargs->frame->getStateTrie();
    trie->addContextStateRevision(ctx, alenumString(param), cvt.ui64);
}

void visit_context_state_changed_string(void *userdata, ALCcontext *ctx, const ALenum param, const ALchar *newval)
{
    newval = cache_string(newval);
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    StateTrie *trie = visitargs->frame->getStateTrie();
    trie->addContextStateRevision(ctx, alenumString(param), (uint64) newval);
}

void visit_listener_state_changed_floatv(void *userdata, ALCcontext *ctx, const ALenum param, const uint32 numfloats, const ALfloat *values)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    const char *paramstr = alenumString(param);
    union { ALfloat f; uint64 ui64; } cvt;
    StateTrie *trie = visitargs->frame->getStateTrie();
    if (numfloats == 1) {
        cvt.f = values[0]; trie->addContextStateRevision(ctx, paramstr, cvt.ui64);
    } else {
        for (uint32 i = 0; i < numfloats; i++) {
            char key[128];
            snprintf(key, sizeof (key), "%s/%u", paramstr, (uint) i);
            cvt.f = values[i];
            trie->addContextStateRevision(ctx, key, cvt.ui64);
        }
    }
}

void visit_source_state_changed_bool(void *userdata, const ALuint name, const ALenum param, const ALboolean newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        trie->addSourceStateRevision(ctx, name, alenumString(param), (uint64) newval);
    }
}

// !!! FIXME: state change events need to specify context if it's something that the mixer changes (AL_STOPPED, offsets, etc).
void visit_source_state_changed_enum(void *userdata, const ALuint name, const ALenum param, const ALenum newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        trie->addSourceStateRevision(ctx, name, alenumString(param), (uint64) newval);
    }
}

void visit_source_state_changed_int(void *userdata, const ALuint name, const ALenum param, const ALint newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    union { ALint i; uint64 ui64; } cvt; cvt.i = newval;
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        trie->addSourceStateRevision(ctx, name, alenumString(param), cvt.ui64);
    }
}

void visit_source_state_changed_uint(void *userdata, const ALuint name, const ALenum param, const ALuint newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        trie->addSourceStateRevision(ctx, name, alenumString(param), (uint64) newval);
    }
}

void visit_source_state_changed_float(void *userdata, const ALuint name, const ALenum param, const ALfloat newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    union { ALfloat f; uint64 ui64; } cvt; cvt.f = newval;
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        trie->addSourceStateRevision(ctx, name, alenumString(param), cvt.ui64);
    }
}

void visit_source_state_changed_float3(void *userdata, const ALuint name, const ALenum param, const ALfloat newval1, const ALfloat newval2, const ALfloat newval3)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    const char *paramstr = alenumString(param);
    const ALfloat values[3] = { newval1, newval2, newval3 };
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCcontext *ctx = trie->getCurrentContext();
    if (ctx) {
        for (int i = 0; i < 3; i++) {
            char key[128];
            snprintf(key, sizeof (key), "%s/%d", paramstr, i);
            union { ALfloat f; uint64 ui64; } cvt; cvt.f = values[i];
            trie->addSourceStateRevision(ctx, name, key, cvt.ui64);
        }
    }
}

void visit_buffer_state_changed_int(void *userdata, const ALuint name, const ALenum param, const ALint newval)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    mark_visit_as_changed_state(visitargs);
    union { ALint i; uint64 ui64; } cvt; cvt.i = newval;
    StateTrie *trie = visitargs->frame->getStateTrie();
    ALCdevice *dev = NULL;
    ALCcontext *ctx = trie->getCurrentContext(&dev);
    if (ctx && dev) {
        trie->addBufferStateRevision(dev, name, alenumString(param), cvt.ui64);
    }
}

void visit_eos(void *userdata, const ALboolean okay, const uint32 wait_until)
{
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    if (visitargs->info) {
        StateTrie *trie = visitargs->frame->getStateTrie();
        visitargs->info->state = trie->snapshotState();  /* lock down state for final call. */
    }
}

int visit_progress(void *userdata, const off_t current, const off_t total)
{
    if (total == 0) return 1;  // uh...
    //printf("PROGRESS: %u / %u\n", (uint) current, (uint) total);
    const int pct = (current == total) ? 100 : ((int) ((((double) current) / ((double) total)) * 100.0));
    VisitArgs *visitargs = ((VisitArgs *) userdata);
    if (visitargs->lastprogresspct == pct) {
        if (now() < visitargs->nextprogressticks) {
            return 1;  // don't spend too much time progressdlg->Update().
        }
    }
    visitargs->lastprogresspct = pct;
    visitargs->nextprogressticks = now() + 100;
    return visitargs->progressdlg->Update(pct) ? 1 : 0;
}

bool ALTraceFrame::openFile(const wxString &path)
{
    #ifdef _WIN32
    const wxString cutdownpath(path.AfterLast('\\').AfterLast('/'));
    #else
    const wxString cutdownpath(path.AfterLast('/'));
    #endif

    SetTitle(wxString::Format("alTrace - %s", cutdownpath));

    wxProgressDialog *progressdlg = new wxProgressDialog(wxT("Loading"), wxT("Loading tracefile, please wait..."), 100, this, wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);

    wxClientDC dc(apiCallGrid);
    dc.SetFont(apiCallGrid->GetFont());

    tracefile_path = path;
    VisitArgs args = { this, progressdlg, NULL, -1, 0, 0, NULL };
    const wxCharBuffer utf8path = path.ToUTF8();

    ALTraceGridUpdateLocker gridlock(apiCallGrid);
    int rc = process_tracelog(utf8path.data(), &args);

    delete progressdlg;

    if (rc == -1) {
        Close(true);  // user clicked "abort" on progress dialog.
        return false;
    } else if (rc == 0) {
        // !!! FIXME: the actual error explanation went to stderr.
        wxMessageBox(wxT("Couldn't process tracefile.\nIt might be missing or corrupt."), wxT("ERROR"));
        Close(true);
        return false;
    }

    // success!
    apiCallGrid->SetTable(apiCallGridTable, false, wxGrid::wxGridSelectRows);

    // AutoSizeColumns() is slowish on large datasets because it has to
    //  generalize and be pixel-perfect. We, however, can cheat a little.
    //apiCallGrid->AutoSizeColumns();

    // these are fast, it only has to measure one string each, so we let it do it, in case it has feelings about minimum padding.
    apiCallGrid->AutoSizeColLabelSize(0);
    apiCallGrid->AutoSizeColLabelSize(1);
    apiCallGrid->AutoSizeColLabelSize(2);

    // For numeric fields, just give yourself room for one digit
    //  more than its biggest number, and use the bigger between that and the
    //  label width. Pad it out by 10 pixels to be safe. Make both the numeric
    //  columns the same size.
    int w, finalsize = 0;
    wxString str;

    str = wxString::Format(wxT("%u"), (uint) (apiCallGridTable->getLargestThreadNum() * 10));
    w = dc.GetTextExtent(str).x;
    if (finalsize < w) finalsize = w;

    str = wxString::Format(wxT("%u"), (uint) (apiCallGridTable->getLatestCallTime() * 10));
    w = dc.GetTextExtent(str).x;
    if (finalsize < w) finalsize = w;

    w = apiCallGrid->GetColSize(0);
    if (finalsize < w) finalsize = w;

    w = apiCallGrid->GetColSize(1);
    if (finalsize < w) finalsize = w;

    finalsize += 10;

    apiCallGrid->SetColSize(0, finalsize);
    apiCallGrid->SetColSize(1, finalsize);

    // Just calculate the extent of the longest string (which usually works out
    //  to be the widest too, although that's not necessarily true).
    finalsize = dc.GetTextExtent(args.longestcallstr).x;
    w = apiCallGrid->GetColSize(2);
    if (finalsize < w) finalsize = w;
    finalsize += 10;

    apiCallGrid->SetColSize(2, finalsize);

    apiCallGrid->SetColMinimalWidth(0, apiCallGrid->GetColSize(0));
    apiCallGrid->SetColMinimalWidth(1, apiCallGrid->GetColSize(1));
    apiCallGrid->SetColMinimalWidth(2, apiCallGrid->GetColSize(2));

    // If smaller than the client size, stretch the callstr column to cover
    //  the difference, otherwise, make it as large as it needs to be to display
    //  the largest string (and the user can scroll horizontally if necessary).
    w = apiCallGrid->GetClientSize().x - (apiCallGrid->GetRowLabelSize() + apiCallGrid->GetColSize(0) + apiCallGrid->GetColSize(1));
    if (w > apiCallGrid->GetColSize(2)) {
        apiCallGrid->SetColSize(2, w);
    }

    return true;
}


class ALTraceApp : public wxApp
{
public:
    ALTraceApp() {}
    virtual bool OnInit();
    virtual int OnExit();

    bool openDocument(const wxString &filename);
    wxString promptForNewFile();
    bool chooseNewFileAndOpen();

    #ifdef __APPLE__
    virtual void MacOpenFiles(const wxArrayString &fileNames);
    virtual void MacNewFile();
    void onMenuOpen(wxCommandEvent &event);
    //void onMenuClose(wxCommandEvent &event);
    void onMenuQuit(wxCommandEvent &event);
    void onMenuAbout(wxCommandEvent &event);
    private:
    wxString openAtLaunch;
    DECLARE_EVENT_TABLE()
    #endif
};

DECLARE_APP(ALTraceApp)
IMPLEMENT_APP(ALTraceApp)


#ifdef __APPLE__
BEGIN_EVENT_TABLE(ALTraceApp, wxApp)
    EVT_MENU(wxID_OPEN, ALTraceApp::onMenuOpen)
//    EVT_MENU(wxID_CLOSE, ALTraceApp::onMenuClose)
    EVT_MENU(wxID_EXIT, ALTraceApp::onMenuQuit)
    EVT_MENU(wxID_ABOUT, ALTraceApp::onMenuAbout)
END_EVENT_TABLE()


void ALTraceApp::onMenuQuit(wxCommandEvent& event)
{
    ExitMainLoop();
}

void ALTraceApp::onMenuOpen(wxCommandEvent& event)
{
    chooseNewFileAndOpen();
}

void ALTraceApp::onMenuAbout(wxCommandEvent &event)
{
    wxString description(wxT("A debugging tool for OpenAL."));

    /*
    if (!openal_loaded) {
        description << wxT("\n(OpenAL not loaded in this process.)");
    } else {
        bool okay = false;
        ALCdevice *dev = REAL_alcOpenDevice(NULL);
        if (dev) {
            ALCcontext *ctx = REAL_alcCreateContext(dev, NULL);
            if (ctx) {
                if (REAL_alcMakeContextCurrent(ctx) == ALC_TRUE) {
                    description << wxT("\nLoaded OpenAL details:");
                    #define ALSTR(x) description << wxString::Format("\n  " #x ": %s", REAL_alGetString(x));
                    ALSTR(AL_VERSION);
                    ALSTR(AL_RENDERER);
                    ALSTR(AL_VENDOR);
                    #undef ALSTR
                    REAL_alcMakeContextCurrent(NULL);
                    okay = true;
                }
                REAL_alcDestroyContext(ctx);
            }
            REAL_alcCloseDevice(dev);
        }
        if (!okay) {
            description << wxT("\n(Couldn't create OpenAL context to determine details.)");
        }
    }
    */

    wxAboutDialogInfo info;
    info.SetName(wxT("alTrace"));
    info.SetVersion(ALTRACE_VERSION);
    info.SetDescription(description);
    info.SetCopyright(wxT("(C) 2019 Ryan C. Gordon <icculus@icculus.org>"));
    ::wxAboutBox(info);
}

void ALTraceApp::MacOpenFiles(const wxArrayString &fileNames)
{
    const size_t total = fileNames.GetCount();
    for (size_t i = 0; i < total; i++) {
        openDocument(fileNames[i]);
    }
}

void ALTraceApp::MacNewFile()
{
    if (!openAtLaunch.IsEmpty()) {
        openDocument(openAtLaunch);
        openAtLaunch = wxT("");
        return;
    }
    //chooseNewFileAndOpen();
}
#endif


wxString ALTraceApp::promptForNewFile()
{
    wxFileDialog dlg(NULL, wxT("Choose a file to open"), wxT(""), wxT(""),
        wxT("alTrace log files (*.altrace)|*.altrace"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    
    if (dlg.ShowModal() == wxID_OK) {
        return dlg.GetPath();
    }
    return wxT("");
}

bool ALTraceApp::openDocument(const wxString &filename)
{
    if (filename.IsEmpty()) {
        return false;
    }
    ALTraceFrame *frame = new ALTraceFrame;
    frame->Show(true);
    return frame->openFile(filename);
}

bool ALTraceApp::chooseNewFileAndOpen()
{
    return openDocument(promptForNewFile());
}


static void loadOpenAL()
{
    if (openal_loaded) {
        return;
    }

    openal_loaded = (load_real_openal() > 0);

    if (!openal_loaded) {
        const wxString cfgstr(wxT("ShowOpenALLoadErrorDialog"));
        wxConfigBase *cfg = wxConfig::Get();
        bool warn = true;
        if (cfg) {
            cfg->Read(cfgstr, &warn, warn);
        }

        if (warn) {
            wxMessageDialogEx dialog(NULL,
                wxT("Couldn't load OpenAL library! Audio playback is disabled."),
                wxT("alTrace"),
                wxOK | (cfg ? wxDISPLAY_NEXT_TIME : 0)
            );
            dialog.ShowModal();

            warn = dialog.GetDisplayNextTime();
            if (cfg) {
                cfg->Write(cfgstr, warn);
                cfg->Flush();
            }
        }
    }
}

#ifdef __APPLE__
static void chdirToAppBundle(const char *argv0)
{
    // !!! FIXME: big hack.
    const bool likelyAppLaunch = strstr(argv0, ".app/Contents/MacOS/") != NULL;
    if (!likelyAppLaunch) { return; }
    char *cpy = strdup(argv0);
    if (cpy) {
        char *ptr = strrchr(cpy, '/');
        if (ptr) {
            *ptr = '\0';
            chdir(cpy); // this might fail, oh well.
        }
        free(cpy);
    }
}
#endif

bool ALTraceApp::OnInit()
{
    SetAppName(wxT("alTrace"));

    appstringcache = stringcache_create();

    #ifdef __APPLE__
    chdirToAppBundle(argv[0]);
    wxApp::SetExitOnFrameDelete(false);
    wxMenuBar *menuBar = new wxMenuBar;
    wxMenu *menuFile = new wxMenu(wxMENU_TEAROFF);
    menuFile->Append(wxID_OPEN);
    menuFile->Append(wxID_CLOSE);
    menuBar->Append(menuFile, _("&File"));
    wxMenu *menuHelp = new wxMenu(wxMENU_TEAROFF);
    menuHelp->Append(wxID_ABOUT);
    menuHelp->Append(wxID_EXIT);
    menuBar->Append(menuHelp, _("&Help"));
    wxMenuBar::MacSetCommonMenuBar(menuBar);
    #endif

    wxConfig::Set(new wxConfig(wxT("alTrace"), wxT("icculus.org")));

    loadOpenAL();

    wxString filename;

    // See if there are any interesting command line options to get started.
    for (int i = 1; i < argc; i++) {
        if ((argv[i][0] != '-') && (filename.IsEmpty())) {
            filename = argv[i];
        } else {
            // add options here.
        }
    }

    #ifdef __APPLE__  // macOS does this in ALTraceApp::MacNewFile()
    openAtLaunch = filename;
    return true;
    #else
    if (!filename.IsEmpty()) {
        return openDocument(filename);
    }
    return chooseNewFileAndOpen();
    #endif
}

int ALTraceApp::OnExit()
{
    free_ioblobs();
    stringcache_destroy(appstringcache);
    appstringcache = NULL;

    if (openal_loaded) {
        close_real_openal();
        openal_loaded = false;
    }

    return wxApp::OnExit();
}

// end of altrace_wxwidgets.cpp ...

