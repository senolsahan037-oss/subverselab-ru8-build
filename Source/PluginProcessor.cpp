#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

using namespace ru8;

juce::AudioProcessorValueTreeState::ParameterLayout RU8AudioProcessor::createLayout() { return params::createLayout(); }

RU8AudioProcessor::RU8AudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RU8", createLayout())
{
    auto raw = [this] (const juce::String& id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    pKit = raw ("kit"); pSet = raw ("set"); pSection = raw ("section"); pAuto = raw ("auto"); pAutoStart = raw ("autoStart");
    pTempo = raw ("tempo"); pSwing = raw ("swing"); pHuman = raw ("human"); pGlue = raw ("glue"); pVolume = raw ("volume");
    pSpaceType = raw ("spaceType"); pSpaceSize = raw ("spaceSize"); pSpaceDamp = raw ("spaceDamp"); pSpacePre = raw ("spacePre");
    pSpaceRet = raw ("spaceReturn"); pEchoTime = raw ("echoTime"); pEchoFb = raw ("echoFeedback"); pEchoTone = raw ("echoTone");
    pEchoRet = raw ("echoReturn"); pTape = raw ("colorTape"); pDust = raw ("colorDust"); pCrush = raw ("colorCrush"); pSource = raw ("source");
    for (int i = 0; i < kNumPads; ++i)
        pp[(size_t) i] = { raw (params::padId (i, "level")), raw (params::padId (i, "tune")), raw (params::padId (i, "decay")),
                           raw (params::padId (i, "pan")), raw (params::padId (i, "space")), raw (params::padId (i, "echo")),
                           raw (params::padId (i, "mute")), raw (params::padId (i, "solo")) };
    for (const char* id : { "kit", "set", "spaceType" }) apvts.addParameterListener (id, this);
    playing = songData.slot[0];
    seq.set = &playing;
    seq.setInitial (Section::A);
    for (auto* q : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q))
        {
            const auto& id = r->paramID;
            if (id == "section" || id == "auto" || id == "autoStart" || id == "source" || id == "tempo" || id.endsWith (".mute") || id.endsWith (".solo")) continue;
            presetParams.push_back (r);
        }
    takePresetSnapshot();   // a new RU-8 is preset 0 (House - Analog) as it comes
    startTimerHz (30);
}

RU8AudioProcessor::~RU8AudioProcessor()
{
    stopTimer();
    for (const char* id : { "kit", "set", "spaceType" }) apvts.removeParameterListener (id, this);
}

bool RU8AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet().isDisabled();
}

void RU8AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setKitSounds (juce::roundToInt (pKit->load()));
    EngineParams ep; readParams (ep, 120.0);
    engine.setParams (ep);
    engine.applyBus (true); engine.applyPads (true);
    setLatencySamples (engine.latency());
    blockStart = 0; wasPlaying = false; lastSectionParam = -1; pendingSlot = -1;
    slotPlaying = slotParam(); playing = songData.slot[(size_t) slotPlaying.load()];
    seq.setInitial ((Section) juce::roundToInt (pSection->load()));
}

void RU8AudioProcessor::readParams (EngineParams& ep, double bpm) const noexcept
{
    for (int i = 0; i < kNumPads; ++i)
    {
        const auto& q = pp[(size_t) i];
        ep.pad[i].levelDb = q.level->load(); ep.pad[i].tune = std::round (q.tune->load()); ep.pad[i].decay = q.decay->load();
        ep.pad[i].pan = q.pan->load(); ep.pad[i].rev = q.space->load(); ep.pad[i].dly = q.echo->load();
        ep.pad[i].mute = q.mute->load() > 0.5f; ep.solo[i] = q.solo->load() > 0.5f;
    }
    ep.glue = pGlue->load(); ep.volumeDb = pVolume->load();
    ep.tape = pTape->load(); ep.dust = pDust->load(); ep.crush = pCrush->load();
    const int type = juce::jlimit (0, 2, juce::roundToInt (pSpaceType->load()));
    ep.space = kSpaceTypes[type];   // hp and lp come with the type
    ep.space.size = pSpaceSize->load(); ep.space.damp = pSpaceDamp->load(); ep.space.pre = pSpacePre->load(); ep.space.ret = pSpaceRet->load();
    ep.echo = { pEchoFb->load(), pEchoTone->load(), pEchoRet->load() };
    ep.echoTime = juce::jlimit (0, 3, juce::roundToInt (pEchoTime->load()));
    ep.bpm = bpm;
}

void RU8AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2) { buffer.clear(); return; }
    const double sr = getSampleRate();

    // ---- transport: the host's, or the standalone one
    bool isPlaying = false, hostPlaying = false; double bpm = pTempo->load(), ppq = 0, barStartPpq = 0;
    if (isStandalone())
    {
        if (standaloneRestart.exchange (false)) standalonePpq = 0;
        isPlaying = standalonePlay.load();
        ppq = standalonePpq;
    }
    else
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) if (*b > 0) bpm = *b;
                hostPlaying = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition()) ppq = *q; else hostPlaying = false;
                if (auto bs = pos->getPpqPositionOfLastBarStart()) barStartPpq = *bs;
            }
        // the preview: RU-8's own clock at the host's tempo while the host is stopped; the host's transport wins when it runs
        if (hostPlaying) previewPlay = false;
        if (standaloneRestart.exchange (false)) standalonePpq = 0;
        // SET -> MIDI: armed by a drag, switched by a pad note arriving while the host plays (that is the clip playing RU-8)
        if (hostPlaying && midiArmed.load())
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn() && ! Sequencer::isTriggerNote (m.getNoteNumber()) && noteToPad (m.getNoteNumber(), padMap.load()) >= 0)
                {
                    midiLatched = true; midiArmed = false;
                    break;
                }
            }
        if (! hostPlaying && previewPlay.load()) { isPlaying = true; ppq = standalonePpq; barStartPpq = 0; }
        else isPlaying = hostPlaying && ! midiSource();   // MIDI: the host's transport no longer runs the set
        // a switch between the two clocks is a new start for the sequencer (its position jumps)
        if (isPlaying && wasPlaying && hostPlaying != lastClockHost) seq.resetCache();
        lastClockHost = hostPlaying;
        dHostPlaying = hostPlaying;
    }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    const double spq = 60.0 / bpm * sr;

    if (resetRequested.exchange (false)) { engine.silence(); seq.resetCache(); }

    // ---- parameters
    EngineParams ep; readParams (ep, bpm);
    engine.setKitSounds (juce::roundToInt (pKit->load()));
    engine.setParams (ep);
    engine.setRunning (isPlaying || hostPlaying);   // the transport runs (dust) also while a clip plays RU-8 in MIDI
    seq.autoArrange = pAuto->load() > 0.5f;
    seq.autoStartBar = juce::roundToInt (pAutoStart->load()) - 1;   // the panel counts bars from 1
    seq.human = pHuman->load();
    const double sectionParam = pSection->load();
    if (sectionParam != lastSectionParam) { seq.requestSection ((Section) juce::roundToInt (sectionParam), (double) blockStart); lastSectionParam = sectionParam; }
    if (isPlaying && ! wasPlaying) seq.resetCache();
    if (fillQueued.exchange (false)) seq.noteRequest (kTriggerFill, (double) blockStart);

    // ---- the set: edits apply at once; another set waits for the next bar line while playing
    const int want = slotParam();
    if (want != slotPlaying.load())
    {
        if (! isPlaying) { slotPlaying = want; pendingSlot = -1; }
        else pendingSlot = want;
    }
    playing = songData.slot[(size_t) slotPlaying.load()];
    playing.swing = pSwing->load();

    // ---- MIDI in: trigger notes to the sequencer, every other note to its pad
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn()) continue;
        const double t = (double) blockStart + meta.samplePosition;
        if (Sequencer::isTriggerNote (m.getNoteNumber())) { seq.noteRequest (m.getNoteNumber(), t); continue; }
        const int pad = noteToPad (m.getNoteNumber(), padMap.load());
        if (pad >= 0)
        {
            const float v = juce::jmax (0.05f, m.getFloatVelocity());
            engine.hitAt (pad, v, t);
            dLastPad = pad; dLastVel = v; dHits = dHits.load() + 1;
        }
    }
    midi.clear();
    // ---- pads from the panel
    {
        const auto scope = hitFifo.read (hitFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) { const auto h = hitBuf[(size_t) (scope.startIndex1 + i)]; engine.hitAt (h.pad, h.vel, (double) blockStart); }
        for (int i = 0; i < scope.blockSize2; ++i) { const auto h = hitBuf[(size_t) (scope.startIndex2 + i)]; engine.hitAt (h.pad, h.vel, (double) blockStart); }
    }

    // ---- the sequencer (the block is cut at a bar line when a new set takes over there)
    if (isPlaying)
    {
        const double barOffset = std::fmod (std::fmod (barStartPpq, 4.0) + 4.0, 4.0);
        const double origin = (double) blockStart - ppq * spq;
        int done = 0;
        if (pendingSlot >= 0)
        {
            const double nextBarPpq = barOffset + 4.0 * std::floor ((ppq - barOffset) / 4.0) + 4.0;
            const double atSample = origin + nextBarPpq * spq - (double) blockStart;
            const double barPpq = barOffset + 4.0 * std::floor ((ppq - barOffset) / 4.0);
            if (std::abs (ppq - barPpq) * spq < 0.5) { slotPlaying = pendingSlot; pendingSlot = -1; }   // the block starts on a bar line
            else if (atSample < n)
            {
                const int cut = juce::jlimit (0, n, (int) std::ceil (atSample));
                seq.process (engine, blockStart, origin, bpm, cut, sr, 1e18, barOffset);
                done = cut;
                slotPlaying = pendingSlot; pendingSlot = -1;
            }
            playing = songData.slot[(size_t) slotPlaying.load()]; playing.swing = pSwing->load();
        }
        seq.process (engine, blockStart + done, origin, bpm, n - done, sr, 1e18, barOffset);
        // display: bar.beat and the 16th that plays
        const double rel = ppq - barOffset;
        const int64_t bar = (int64_t) std::floor (rel / 4.0);
        int sec; bool fill, on; seq.barPlan (bar, origin + (barOffset + bar * 4.0) * spq, sec, fill, on);
        dBar = (int) bar; dStep = (int) std::floor ((rel - bar * 4.0) * 4.0); dSection = on ? sec + 1 : 0; dFill = fill;
    }
    else { dStep = -1; dSection = (int) seq.current(); }
    wasPlaying = isPlaying;
    if (isPlaying && (isStandalone() || ! hostPlaying)) standalonePpq += n / spq;

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    blockStart += n;
    dPlaying = isPlaying; dBpm = (float) bpm; dPeak = engine.lastPeak; dGr = engine.lastGr;
}

RU8AudioProcessor::Display RU8AudioProcessor::display() const noexcept
{
    Display d;
    d.playing = dPlaying.load(); d.hostPlaying = dHostPlaying.load(); d.bar = dBar.load(); d.step = dStep.load(); d.section = dSection.load(); d.fill = dFill.load();
    d.bpm = dBpm.load(); d.peak = dPeak.load(); d.gr = dGr.load(); d.lastHitPad = dLastPad.load(); d.hitCounter = dHits.load(); d.lastHitVel = dLastVel.load();
    return d;
}

void RU8AudioProcessor::hitPad (int pad, float velocity)
{
    if (pad < 0 || pad >= kNumPads) return;
    const auto scope = hitFifo.write (1);
    if (scope.blockSize1 > 0) hitBuf[(size_t) scope.startIndex1] = { pad, velocity };
    else if (scope.blockSize2 > 0) hitBuf[(size_t) scope.startIndex2] = { pad, velocity };
    dLastPad = pad; dLastVel = velocity; dHits = dHits.load() + 1;
}

void RU8AudioProcessor::setStandalonePlaying (bool on)
{
    if (on && ! standalonePlay.load()) standaloneRestart = true;
    standalonePlay = on;
}

void RU8AudioProcessor::setPreviewPlaying (bool on)
{
    if (isStandalone()) return;
    if (on && dHostPlaying.load()) return;            // the host's transport runs: it plays RU-8
    if (on && ! previewPlay.load()) standaloneRestart = true;
    previewPlay = on;
}

void RU8AudioProcessor::startStop()
{
    if (isStandalone()) setStandalonePlaying (! standalonePlay.load());
    else setPreviewPlaying (! previewPlay.load());
}

void RU8AudioProcessor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        setParam ("tempo", (float) juce::jlimit (50.0, 200.0, 60.0 / avg));
    }
}

MidiClip RU8AudioProcessor::makeClip (bool full, int pad) const
{
    SetPatterns set = songData.slot[(size_t) slotParam()];
    set.swing = pSwing->load();
    const bool song = pAuto->load() > 0.5f;
    const int sec = juce::roundToInt (pSection->load()) == 2 ? 1 : 0;
    const double bpm = isStandalone() ? (double) pTempo->load() : (double) dBpm.load();
    const int kit = juce::roundToInt (pKit->load());
    const juce::String setName = params::setNames()[slotParam()];
    juce::String padName;
    if (! full && pad >= 0 && pad < kNumPads) padName = padNameFor (pad, kit);
    return makeMidiClip (set, setName.toRawUTF8(), kKits[kit].name, song, sec, full ? -1 : pad, padMap.load(), bpm, full ? nullptr : padName.toRawUTF8());
}

juce::String RU8AudioProcessor::padNameFor (int pad, int kit)
{
    static const char* const core[8] = { "Kick", "Snare", "Clap", "Rim", "Closed hat", "Open hat", "Low tom", "High tom" };
    if (pad < 0 || pad >= kNumPads) return {};
    return pad < 8 ? juce::String (core[pad]) : juce::String (kKits[juce::jlimit (0, kNumKits - 1, kit)].aux[pad - 8].name);
}

std::optional<juce::String> RU8AudioProcessor::getNameForMidiNoteNumber (int note, int)
{
    if (note < 0 || note > 127) return std::nullopt;   // hosts may ask outside 0-127
    switch (note)
    {
        case kTriggerA:    return juce::String ("RU-8 section A");
        case kTriggerB:    return juce::String ("RU-8 section B");
        case kTriggerFill: return juce::String ("RU-8 fill");
        case kTriggerOff:  return juce::String ("RU-8 off");
        default: break;
    }
    const int pad = noteToPad (note, padMap.load());
    if (pad < 0) return std::nullopt;
    return padNameFor (pad, juce::roundToInt (pKit->load()));
}

float RU8AudioProcessor::kitDefault (const juce::String& id) const
{
    const int k = juce::roundToInt (pKit->load());
    const KitDef& kd = kKits[k];
    for (int i = 0; i < kNumPads; ++i)
    {
        if (id == params::padId (i, "level")) return kd.ch[i].levelDb;
        if (id == params::padId (i, "pan")) return kd.ch[i].pan;
        if (id == params::padId (i, "space")) return kd.ch[i].rev;
        if (id == params::padId (i, "echo")) return kd.ch[i].dly;
        if (id == params::padId (i, "tune")) return 0.0f;
        if (id == params::padId (i, "decay")) return 1.0f;
    }
    if (id == "glue") return kd.glue;
    if (id == "colorTape") return kd.color.tape;
    if (id == "colorDust") return kd.color.dust;
    if (id == "colorCrush") return kd.color.crush;
    if (id == "spaceSize") return kd.space.size;
    if (id == "spaceDamp") return kd.space.damp;
    if (id == "spacePre") return kd.space.pre;
    if (id == "spaceReturn") return kd.space.ret;
    if (id == "echoFeedback") return kd.echo.fb;
    if (id == "echoTone") return kd.echo.tone;
    if (id == "echoReturn") return kd.echo.ret;
    const auto& s = songData.slot[(size_t) slotParam()];
    if (id == "tempo") return s.bpm;
    if (id == "swing") return s.swing;
    if (auto* p = apvts.getParameter (id)) return p->convertFrom0to1 (p->getDefaultValue());
    return 0.0f;
}

// ---- kit, set and SPACE type move other parameters (message thread)
void RU8AudioProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "kit") kitChanged = true;
    else if (id == "set") setChanged = true;
    else if (id == "spaceType") typeChanged = true;
}

void RU8AudioProcessor::timerCallback()
{
    if (restoring) return;
    const int kit = juce::roundToInt (pKit->load()), set = slotParam(), type = juce::roundToInt (pSpaceType->load());
    if (kitChanged.exchange (false) && kit != appliedKit) { appliedKit = kit; loadKitValues (kit); appliedType = juce::roundToInt (pSpaceType->load()); }
    if (setChanged.exchange (false) && set != appliedSet) { appliedSet = set; loadSetValues (set); }
    if (typeChanged.exchange (false) && type != appliedType) { appliedType = type; loadSpaceType (type); }
    if (midiLatched.load()) { setParam ("source", 1.0f); midiLatched = false; }   // the proof came on the audio thread
    if (const int prog = pendingProgram.exchange (-1); prog >= 0) loadPreset (prog);
    checkPresetModified();
    timerTicks.fetch_add (1);
}

// ---- factory presets
const juce::String RU8AudioProcessor::getProgramName (int index)
{
    return index >= 0 && index < kNumPresets ? juce::String (kPresets[index].name) : juce::String();
}

void RU8AudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumPresets || index == presetIndex.load()) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) loadPreset (index);
    else pendingProgram = index;   // the timer loads it on the message thread
}

void RU8AudioProcessor::loadPreset (int index)
{
    if (index < 0 || index >= kNumPresets) return;
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    if (beforePresetLoad) beforePresetLoad();
    const PresetDef& d = kPresets[index];
    const int kit = presetKit (index), set = presetSet (index);
    {
        svl::ParamUndo::Group step (undo, juce::String ("Preset ") + d.name);
        songData.reload (set);                       // the groove: the set's factory steps
        setParam ("kit", (float) kit);
        appliedKit = kit; kitChanged = false;
        loadKitValues (kit);
        setParam ("set", (float) set);               // while playing the set lands on the next bar line (processBlock)
        appliedSet = set; setChanged = false;
        loadSetValues (set);
        setParam ("human", 0.12f);
        setParam ("volume", -2.0f);
        for (const auto& v : d.values)
            if (v.param != nullptr) setParam (v.param, v.value);
        appliedType = juce::roundToInt (pSpaceType->load()); typeChanged = false;
    }
    presetIndex = index;
    presetDirtyLatched = false;
    takePresetSnapshot();
    presetDirty = false;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void RU8AudioProcessor::takePresetSnapshot()
{
    presetSnapshot.resize (presetParams.size());
    for (size_t i = 0; i < presetParams.size(); ++i) presetSnapshot[i] = presetParams[i]->getValue();
    presetSnapshotSet = slotParam();
    presetSnapshotSteps = songData.slot[(size_t) presetSnapshotSet];
}

void RU8AudioProcessor::checkPresetModified()
{
    bool dirty = presetDirtyLatched;
    for (size_t i = 0; i < presetParams.size() && ! dirty; ++i)
        dirty = std::abs (presetParams[i]->getValue() - presetSnapshot[i]) > 1.0e-6f;
    if (! dirty) dirty = std::memcmp (songData.slot[(size_t) presetSnapshotSet].steps, presetSnapshotSteps.steps, sizeof presetSnapshotSteps.steps) != 0;
    presetDirty = dirty;
}

void RU8AudioProcessor::disarmMidiSource() noexcept { midiArmed = false; midiLatched = false; }

void RU8AudioProcessor::setParam (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float v = p->convertTo0to1 (plain);
        if (std::abs (p->getValue() - v) < 1.0e-7f) return;
        p->beginChangeGesture(); p->setValueNotifyingHost (v); p->endChangeGesture();
    }
}

void RU8AudioProcessor::loadKitValues (int k)
{
    const KitDef& kd = kKits[juce::jlimit (0, kNumKits - 1, k)];
    for (int i = 0; i < kNumPads; ++i)
    {
        setParam (params::padId (i, "level"), kd.ch[i].levelDb); setParam (params::padId (i, "tune"), 0.0f);
        setParam (params::padId (i, "decay"), 1.0f); setParam (params::padId (i, "pan"), kd.ch[i].pan);
        setParam (params::padId (i, "space"), kd.ch[i].rev); setParam (params::padId (i, "echo"), kd.ch[i].dly);
    }
    setParam ("glue", kd.glue);
    setParam ("spaceType", (float) kd.space.type);
    setParam ("spaceSize", kd.space.size); setParam ("spaceDamp", kd.space.damp); setParam ("spacePre", kd.space.pre); setParam ("spaceReturn", kd.space.ret);
    setParam ("echoFeedback", kd.echo.fb); setParam ("echoTone", kd.echo.tone); setParam ("echoReturn", kd.echo.ret);
    setParam ("colorTape", kd.color.tape); setParam ("colorDust", kd.color.dust); setParam ("colorCrush", kd.color.crush);
}

void RU8AudioProcessor::loadSetValues (int slot)
{
    const auto& s = songData.slot[(size_t) juce::jlimit (0, kNumSlots - 1, slot)];
    if (isStandalone()) setParam ("tempo", s.bpm);
    setParam ("swing", s.swing);
    setParam ("echoTime", (float) s.echoTime);
}

void RU8AudioProcessor::loadSpaceType (int type)
{
    const SpaceDef& d = kSpaceTypes[juce::jlimit (0, 2, type)];
    setParam ("spaceSize", d.size); setParam ("spaceDamp", d.damp); setParam ("spacePre", d.pre); setParam ("spaceReturn", d.ret);
}

juce::AudioProcessorEditor* RU8AudioProcessor::createEditor() { return new RU8Editor (*this); }

void RU8AudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("song", juce::String (songData.save (slotParam(), juce::roundToInt (pKit->load()), padMap.load())), nullptr);
    state.setProperty ("editorWidth", editorWidth.load(), nullptr);
    state.setProperty ("mood", mood, nullptr);
    state.setProperty ("preset", presetIndex.load(), nullptr);
    state.setProperty ("presetModified", presetDirty.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void RU8AudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            restoring = true;
            int slot = 0, kit = 0; bool pads = false;
            const auto text = tree.getProperty ("song").toString().toStdString();
            if (! text.empty() && songData.load (text, slot, kit, pads)) padMap = pads;
            editorWidth = juce::jlimit (590, 2360, (int) tree.getProperty ("editorWidth", editorWidth.load()));
            mood = tree.getProperty ("mood", mood).toString();
            const int preset = juce::jlimit (0, kNumPresets - 1, (int) tree.getProperty ("preset", 0));
            const bool presetWasModified = (bool) tree.getProperty ("presetModified", false);
            tree.removeProperty ("song", nullptr);
            tree.removeProperty ("preset", nullptr);
            tree.removeProperty ("presetModified", nullptr);
            apvts.replaceState (tree);
            // the restored values are the project's: a kit or set change caused by the restore must not reset them
            appliedKit = juce::roundToInt (pKit->load()); appliedSet = slotParam(); appliedType = juce::roundToInt (pSpaceType->load());
            kitChanged = setChanged = typeChanged = false;
            midiArmed = false; midiLatched = false;
            // the program the project had; a host that sets it again changes nothing (setCurrentProgram)
            presetIndex = preset; pendingProgram = -1;
            presetDirtyLatched = presetWasModified;
            takePresetSnapshot();
            presetDirty = presetWasModified;
            restoring = false;
            undo.clear();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RU8AudioProcessor(); }
