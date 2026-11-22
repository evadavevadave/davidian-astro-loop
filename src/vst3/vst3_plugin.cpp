/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* VST3 build of the Astro-Loop: an audio processor around engine.c, an edit controller whose
 * parameters come from params.h, and an editor that hosts the same panel as the LV2 GUI.
 * Built on the Steinberg VST3 SDK (MIT licence; see NOTICE.md). */
#include <cstring>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

extern "C" {
#include "../engine.h"
}
#include "../params.h"
#include "../ui/view.h"

#ifndef AL_VERSION
#    define AL_VERSION "0.0.0"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

/* The VST3 class IDs. They must never change, or hosts lose saved projects. */
const FUID kProcessorUID(0x18EF4604, 0xBEB34D89, 0xA2D74EAC, 0xE7E61890);
const FUID kControllerUID(0x5689608F, 0xD0CD4F71, 0xBBD393ED, 0x0D978234);

/* State: a tag, a count, then every input control's plain value in port order. */
const int32 kStateTag = 0x41534C31; /* "ASL1" */

/* VST3 has its own bypass parameter, the inverse of the LV2 "enabled" port, under that port's ID. */
const ParamID kBypassID = PORT_ENABLED;

void ascii_to_tchar(const char* s, TChar* dst, int max)
{
    int i = 0;
    for (; s && s[i] && i < max - 1; ++i)
        dst[i] = (TChar)(unsigned char)s[i];
    dst[i] = 0;
}

void tchar_to_ascii(const TChar* s, char* dst, int max)
{
    int i = 0;
    for (; s && s[i] && i < max - 1; ++i)
        dst[i] = s[i] < 128 ? (char)s[i] : '?';
    dst[i] = 0;
}

/* Reads a saved state into plain values; leaves them alone if the state isn't ours. */
bool read_state(IBStream* state, float* values)
{
    IBStreamer s(state, kLittleEndian);
    int32 tag = 0, n = 0;
    float v[AL_NUM_INPUTS];
    if (!s.readInt32(tag) || tag != kStateTag || !s.readInt32(n) || n < 1)
        return false;
    for (int32 i = 0; i < AL_NUM_INPUTS; ++i) {
        const AlParam* p = al_param(AL_FIRST_CONTROL + i);
        v[i] = p->def; /* controls added in later versions keep their defaults */
        if (i < n && !s.readFloat(v[i]))
            return false;
        v[i] = al_clamp(p, v[i]);
    }
    std::memcpy(values + AL_FIRST_CONTROL, v, sizeof v);
    return true;
}

/* ---- Processor ---- */

class Processor : public AudioEffect {
public:
    Processor()
    {
        setControllerClass(kControllerUID);
        for (int i = 0; i < AL_NUM_PARAMS; ++i)
            values[AL_PARAMS[i].port] = AL_PARAMS[i].def;
    }
    ~Processor() override { al_engine_free(engine); }

    static FUnknown* createInstance(void*) { return (IAudioProcessor*)new Processor; }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = AudioEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        return kResultOk;
    }

    /* Stereo out, from a stereo or a mono input (a mono input feeds both channels). */
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                          SpeakerArrangement* outputs, int32 numOuts) override
    {
        if (numIns != 1 || numOuts != 1 || outputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        if (inputs[0] != SpeakerArr::kStereo && inputs[0] != SpeakerArr::kMono)
            return kResultFalse;
        if (AudioBus* bus = getAudioInput(0)) {
            bus->setArrangement(inputs[0]);
            bus->setName(inputs[0] == SpeakerArr::kMono ? STR16("Mono In") : STR16("Stereo In"));
        }
        return kResultTrue;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        sample_rate = setup.sampleRate;
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) override
    {
        if (state) {
            al_engine_free(engine);
            engine = al_engine_new(sample_rate);
            if (!engine)
                return kOutOfMemory;
            for (int port = AL_FIRST_CONTROL; port < PORT_COUNT; ++port)
                al_engine_connect(engine, (uint32_t)port, &values[port]);
            al_engine_activate(engine);
            sent_vu = -1.0f;
        }
        return AudioEffect::setActive(state);
    }

    uint32 PLUGIN_API getTailSamples() override { return (uint32)(10.0 * sample_rate); } /* repeats and springs */

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (IParameterChanges* changes = data.inputParameterChanges) {
            const int32 n = changes->getParameterCount();
            for (int32 i = 0; i < n; ++i) {
                IParamValueQueue* q = changes->getParameterData(i);
                int32 offset = 0, points = q ? q->getPointCount() : 0;
                ParamValue v = 0.0;
                /* The controls are smoothed per sample, so the block's last value is enough. */
                if (points <= 0 || q->getPoint(points - 1, offset, v) != kResultTrue)
                    continue;
                apply(q->getParameterId(), v);
            }
        }
        if (!engine || data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1
            || data.symbolicSampleSize != kSample32)
            return kResultOk;

        {
            AudioBusBuffers& in = data.inputs[0];
            AudioBusBuffers& out = data.outputs[0];
            if (in.numChannels < 1 || out.numChannels < 2)
                return kResultOk;
            Sample32* ia = in.channelBuffers32[0];
            Sample32* ib = in.numChannels > 1 ? in.channelBuffers32[1] : ia;
            al_engine_connect(engine, PORT_IN_L, ia);
            al_engine_connect(engine, PORT_IN_R, ib);
            al_engine_connect(engine, PORT_OUT_L, out.channelBuffers32[0]);
            al_engine_connect(engine, PORT_OUT_R, out.channelBuffers32[1]);
            al_engine_run(engine, (uint32_t)data.numSamples);
            out.silenceFlags = 0;
        }

        /* The VU meter goes to the editor as a read-only parameter. */
        if (IParameterChanges* outs = data.outputParameterChanges) {
            const float v = values[PORT_VU];
            int32 index = 0;
            if (v != sent_vu) {
                if (IParamValueQueue* q = outs->addParameterData(PORT_VU, index)) {
                    q->addPoint(0, v, index);
                    sent_vu = v;
                }
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        float v[PORT_COUNT];
        std::memcpy(v, values, sizeof v);
        if (!read_state(state, v))
            return kResultFalse;
        std::memcpy(values + AL_FIRST_CONTROL, v + AL_FIRST_CONTROL, AL_NUM_INPUTS * sizeof(float));
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        if (!s.writeInt32(kStateTag) || !s.writeInt32(AL_NUM_INPUTS))
            return kResultFalse;
        for (int i = 0; i < AL_NUM_INPUTS; ++i)
            if (!s.writeFloat(values[AL_FIRST_CONTROL + i]))
                return kResultFalse;
        return kResultOk;
    }

private:
    void apply(ParamID id, ParamValue v)
    {
        if (id == kBypassID) {
            values[PORT_ENABLED] = v >= 0.5 ? 0.0f : 1.0f;
            return;
        }
        const AlParam* p = al_param((int)id);
        if (p && !p->output)
            values[id] = al_from_norm(p, v);
    }

    AlEngine* engine = nullptr;
    double sample_rate = 48000.0;
    float values[PORT_COUNT] = {};
    float sent_vu = -1.0f;
};

/* ---- Controller and editor ---- */

class AlParameter : public Parameter {
public:
    explicit AlParameter(const AlParam* bp) : p(bp)
    {
        info.id = (ParamID)p->port;
        ascii_to_tchar(p->name, info.title, 128);
        ascii_to_tchar(p->short_name, info.shortTitle, 128);
        ascii_to_tchar(p->unit, info.units, 128);
        info.stepCount = p->steps;
        info.defaultNormalizedValue = al_to_norm(p, p->def);
        info.unitId = kRootUnitId;
        info.flags = p->output ? ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden
                               : ParameterInfo::kCanAutomate | (p->steps ? ParameterInfo::kIsList : 0);
        valueNormalized = info.defaultNormalizedValue;
    }

    void toString(ParamValue norm, String128 string) const override
    {
        char buf[64];
        al_format(p, al_from_norm(p, norm), buf, sizeof buf);
        ascii_to_tchar(buf, string, 128);
    }

    bool fromString(const TChar* string, ParamValue& norm) const override
    {
        char buf[64];
        float v = 0.0f;
        tchar_to_ascii(string, buf, sizeof buf);
        if (!al_parse(p, buf, &v))
            return false;
        norm = al_to_norm(p, v);
        return true;
    }

    ParamValue toPlain(ParamValue norm) const override { return al_from_norm(p, norm); }
    ParamValue toNormalized(ParamValue plain) const override { return al_to_norm(p, (float)plain); }

private:
    const AlParam* p;
};

class Editor;

class Controller : public EditController {
public:
    static FUnknown* createInstance(void*) { return (IEditController*)new Controller; }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk)
            return r;
        for (int i = 0; i < AL_NUM_PARAMS; ++i)
            if (AL_PARAMS[i].port != PORT_ENABLED)
                parameters.addParameter(new AlParameter(&AL_PARAMS[i]));
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                                ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassID);
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        float v[PORT_COUNT];
        if (!state || !read_state(state, v))
            return kResultFalse;
        for (int i = 0; i < AL_NUM_INPUTS; ++i) {
            const int port = AL_FIRST_CONTROL + i;
            if (port == PORT_ENABLED)
                setParamNormalized(kBypassID, v[port] > 0.5f ? 0.0 : 1.0);
            else
                setParamNormalized((ParamID)port, al_to_norm(al_param(port), v[port]));
        }
        return kResultOk;
    }

    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override;
    IPlugView* PLUGIN_API createView(FIDString name) override;

    /* The panel's view of a parameter, as a plain value. */
    float plain(int port)
    {
        const AlParam* p = al_param(port);
        return p ? al_from_norm(p, getParamNormalized((ParamID)port)) : 0.0f;
    }

    Editor* editor = nullptr;
};

class Editor : public CPluginView {
public:
    explicit Editor(Controller* c) : CPluginView(nullptr), controller(c)
    {
        ViewRect r(0, 0, AL_VIEW_W, AL_VIEW_H);
        setRect(r);
    }
    ~Editor() override { close(); }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#if defined(_WIN32)
        return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#elif defined(__APPLE__)
        return std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
#else
        /* X11. The host's run loop (from the plug frame, once attached) drives events and redraws. */
        return std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ? kResultTrue : kResultFalse;
#endif
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        AlViewHost host = {};
        if (isPlatformTypeSupported(type) != kResultTrue || view)
            return kResultFalse;
        host.host = this;
        host.write = on_write;
        host.gesture = on_gesture;
        view = al_view_open(parent, &host);
        if (!view)
            return kResultFalse;
        for (int i = 0; i < AL_NUM_PARAMS; ++i)
            if (AL_PARAMS[i].port != PORT_ENABLED)
                al_view_set(view, AL_PARAMS[i].port, controller->plain(AL_PARAMS[i].port));
        controller->editor = this;
#if !defined(_WIN32) && !defined(__APPLE__)
        timer.editor = this;
        if (FUnknownPtr<Linux::IRunLoop> loop = FUnknownPtr<Linux::IRunLoop>(plugFrame))
            loop->registerTimer(&timer, 30);
#endif
        return CPluginView::attached(parent, type);
    }

    tresult PLUGIN_API removed() override
    {
        close();
        return CPluginView::removed();
    }

    tresult PLUGIN_API canResize() override { return kResultFalse; }

    void set(int port, float value)
    {
        if (view)
            al_view_set(view, port, value);
    }

private:
#if !defined(_WIN32) && !defined(__APPLE__)
    /* The host's run loop calls this about 30 times a second; the X11 panel has no thread of
     * its own, so this is where it reads its events and redraws. */
    struct Timer : Linux::ITimerHandler {
        Editor* editor = nullptr;
        void PLUGIN_API onTimer() override
        {
            if (editor && editor->view)
                al_view_idle(editor->view);
        }
        tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
        {
            if (FUnknownPrivate::iidEqual(iid, Linux::ITimerHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
                *obj = this;
                return kResultOk;
            }
            *obj = nullptr;
            return kNoInterface;
        }
        /* Owned by the editor, which unregisters it before it goes. */
        uint32 PLUGIN_API addRef() override { return 1; }
        uint32 PLUGIN_API release() override { return 1; }
    } timer;
#endif

    void close()
    {
#if !defined(_WIN32) && !defined(__APPLE__)
        if (view)
            if (FUnknownPtr<Linux::IRunLoop> loop = FUnknownPtr<Linux::IRunLoop>(plugFrame))
                loop->unregisterTimer(&timer);
#endif
        if (controller->editor == this)
            controller->editor = nullptr;
        al_view_close(view);
        view = nullptr;
    }

    static void on_write(void* host, int port, float value)
    {
        Editor* e = (Editor*)host;
        const ParamValue norm = al_to_norm(al_param(port), value);
        e->controller->setParamNormalized((ParamID)port, norm);
        e->controller->performEdit((ParamID)port, norm);
    }

    static void on_gesture(void* host, int port, int begin)
    {
        Editor* e = (Editor*)host;
        if (begin)
            e->controller->beginEdit((ParamID)port);
        else
            e->controller->endEdit((ParamID)port);
    }

    IPtr<Controller> controller;
    AlView* view = nullptr;
};

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value)
{
    const tresult r = EditController::setParamNormalized(tag, value);
    if (editor && tag != kBypassID)
        editor->set((int)tag, plain((int)tag));
    return r;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name)
{
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new Editor(this);
    return nullptr;
}

} // namespace

/* ---- Factory ---- */

BEGIN_FACTORY_DEF("Davidian", "https://github.com/evadavevadave/davidian-astro-loop", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Astro-Loop", Vst::kDistributable, PlugType::kFxDelay, AL_VERSION, kVstVersionString,
           Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "Astro-Loop Controller", 0, "", AL_VERSION, kVstVersionString, Controller::createInstance)

END_FACTORY
