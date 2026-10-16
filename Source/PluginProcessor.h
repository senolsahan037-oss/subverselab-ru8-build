// RU-8 plug-in shell (phase 3). Wraps the plain engine (RU8Engine.h) and sequencer (RU8Sequencer.h); no DSP lives here.
#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "RU8Sequencer.h"
#include "RU8Presets.h"
#include "SvlUndo.h"

class RU8AudioProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::Timer
{
public:
    RU8AudioProcessor();
    ~RU8AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { resetRequested = true; }   // a host's reset (transport stop / jump): voices and tails to rest on the next block
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RU-8"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }   // SPACE and ECHO ring on

    // VST3 note names (hosts that read them show pad names in their editors): the note map and the kit decide (rule 6a)
    std::optional<juce::String> getNameForMidiNoteNumber (int note, int midiChannel) override;
    static juce::String padNameFor (int pad, int kit);       // Kick ... High tom, then the kit's aux names

    // The 28 factory presets are the host's programs. A host that sets the program already current (some do after restoring a
    // project) changes nothing; only a real change loads.
    int getNumPrograms() override { return ru8::kNumPresets; }
    int getCurrentProgram() override { return presetIndex.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    svl::ParamUndo undo { *this };   // Cmd+Z / Cmd+Shift+Z: editor edits only (SvlUndo.h)

    // ---- for the panel (message thread) ----
    ru8::Song& song() noexcept { return songData; }          // steps of every set; a step is one byte, written by the panel, read by the audio thread
    int currentSlot() const noexcept { return slotPlaying.load(); }
    int editSlot() const noexcept { return slotParam(); }   // the set the panel shows and edits (it plays from the next bar)
    void hitPad (int pad, float velocity);                   // a pad played from the panel or the computer keyboard
    void setStandalonePlaying (bool on);                     // standalone transport (a host's transport is followed otherwise)
    void setPreviewPlaying (bool on);                        // in a host: RU-8's own clock at the host's tempo while the host is stopped
    void startStop();                                        // the play key: standalone transport, or the preview in a host
    // SET / MIDI (host only): a drag of a clip with notes arms MIDI; a pad note while the host plays and it is armed switches to MIDI.
    // MIDI: the host's transport no longer runs the sequencer; MIDI still plays the pads; the preview still plays the set.
    void armMidiSource() noexcept { if (! isStandalone()) midiArmed = true; }
    bool midiSourceArmed() const noexcept { return midiArmed.load(); }
    void disarmMidiSource() noexcept;                        // the panel's SET / MIDI switch (before it writes the parameter)
    bool midiSource() const noexcept { return pSource->load() > 0.5f || midiLatched.load(); }
    bool isStandalone() const noexcept { return wrapperType == wrapperType_Standalone; }
    void tap();                                              // TAP tempo (standalone)
    void queueFill() noexcept { fillQueued = true; }         // the FILL key: one bar of fill on the next bar line (as E-1)
    bool padNoteMap() const noexcept { return padMap.load(); }
    void setPadNoteMap (bool pads) noexcept { padMap.store (pads); }
    ru8::MidiClip makeClip (bool full, int pad) const;       // the clip of what plays (auto: the 8-bar arrangement; else one bar)
    // factory presets (message thread): load one (one undo step, the set lands on the next bar line while playing), and what the
    // panel shows: the loaded preset and whether anything changed since (parameters the preset holds, or the steps of its set)
    void loadPreset (int index);
    int currentPreset() const noexcept { return presetIndex.load(); }
    bool presetModified() const noexcept { return presetDirty.load(); }
    std::function<void()> beforePresetLoad;                  // the editor arms the knobs' glide (the product is about to write values)
    // the kit's value for a parameter (double-click returns a knob to it)
    float kitDefault (const juce::String& paramId) const;
    // what the readout shows
    struct Display { bool playing = false, hostPlaying = false; int bar = 0, step = -1, section = 1; bool fill = false; float bpm = 120, peak = 0, gr = 0; int lastHitPad = -1; uint32_t hitCounter = 0; float lastHitVel = 0; };
    Display display() const noexcept;
    std::atomic<int> editorWidth { 1180 };
    juce::String mood { "cream" };

    // test hooks (plugin_check): the engine and sequencer as the audio thread uses them
    ru8::Engine& engineForTest() noexcept { return engine; }
    // test hooks (plugin_check, ui_snapshot): the 30 Hz timer's message-thread work. A check waits until a complete timer callback has
    // run after its change and nothing is left to apply, instead of a fixed time (a busy machine can delay a timer past any fixed time).
    uint32_t timerTicksForTest() const noexcept { return timerTicks.load(); }
    bool messageWorkPendingForTest() const noexcept
    {
        return kitChanged.load() || setChanged.load() || typeChanged.load() || midiLatched.load() || pendingProgram.load() >= 0;
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String& id, float value) override;
    void timerCallback() override;   // message thread, 30 Hz: applies kit / set / SPACE type loads (no posting from the audio thread)
    void loadKitValues (int kit);       // moves every value a kit sets (message thread, notifying the host)
    void loadSetValues (int slot);      // tempo (standalone), swing, echo time of a set
    void loadSpaceType (int type);      // a SPACE type loads its size, damp, pre-delay and return
    void setParam (const juce::String& id, float plainValue);
    void takePresetSnapshot();          // what "not modified" means: the values and the set's steps right after a load or restore
    void checkPresetModified();         // timer: compares against the snapshot
    int slotParam() const noexcept { return juce::roundToInt (pSet->load()); }
    void readParams (ru8::EngineParams& ep, double bpm) const noexcept;

    ru8::Engine engine;
    ru8::Sequencer seq;
    ru8::Song songData;
    ru8::SetPatterns playing;           // the set the sequencer plays (a copy, swapped on the bar line)
    std::atomic<int> slotPlaying { 0 };
    int pendingSlot = -1;
    bool wasPlaying = false;
    double lastSectionParam = -1;
    int64_t blockStart = 0;
    double standalonePpq = 0;
    std::atomic<bool> standalonePlay { false }, standaloneRestart { false };
    std::atomic<bool> previewPlay { false };
    std::atomic<bool> midiArmed { false }, midiLatched { false };   // latched: the audio thread saw the proof; the timer writes the parameter                 // in a host only; the host's transport stops it when it starts
    bool lastClockHost = false;
    std::atomic<bool> padMap { false }, fillQueued { false }, resetRequested { false };

    // panel hits, handed to the audio thread
    struct PadHit { int pad; float vel; };
    juce::AbstractFifo hitFifo { 64 };
    std::array<PadHit, 64> hitBuf {};

    // display
    std::atomic<bool> dPlaying { false }, dHostPlaying { false }, dFill { false };
    std::atomic<int> dBar { 0 }, dStep { -1 }, dSection { 1 }, dLastPad { -1 };
    std::atomic<uint32_t> dHits { 0 };
    std::atomic<float> dBpm { 120 }, dPeak { 0 }, dGr { 0 }, dLastVel { 0 };

    // message-thread work requested from parameterChanged (any thread, including the audio thread: flags only)
    std::atomic<bool> kitChanged { false }, setChanged { false }, typeChanged { false };
    int appliedKit = 0, appliedSet = 0, appliedType = 0;
    bool restoring = false;

    std::vector<double> taps;
    std::atomic<int> presetIndex { 0 }, pendingProgram { -1 };
    std::atomic<bool> presetDirty { false };
    std::atomic<uint32_t> timerTicks { 0 };   // complete timer callbacks (test hook above)
    bool presetDirtyLatched = false;    // restored as modified: stays modified until a preset loads
    std::vector<juce::RangedAudioParameter*> presetParams;   // the parameters a preset holds (not mute, solo, source, section, auto, autoStart, tempo)
    std::vector<float> presetSnapshot;
    int presetSnapshotSet = 0;
    ru8::SetPatterns presetSnapshotSteps {};
    std::atomic<float>* pKit = nullptr; std::atomic<float>* pSet = nullptr; std::atomic<float>* pSection = nullptr;
    std::atomic<float>* pAuto = nullptr; std::atomic<float>* pAutoStart = nullptr; std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pSwing = nullptr; std::atomic<float>* pHuman = nullptr; std::atomic<float>* pGlue = nullptr;
    std::atomic<float>* pVolume = nullptr; std::atomic<float>* pSpaceType = nullptr; std::atomic<float>* pSpaceSize = nullptr;
    std::atomic<float>* pSpaceDamp = nullptr; std::atomic<float>* pSpacePre = nullptr; std::atomic<float>* pSpaceRet = nullptr;
    std::atomic<float>* pEchoTime = nullptr; std::atomic<float>* pEchoFb = nullptr; std::atomic<float>* pEchoTone = nullptr;
    std::atomic<float>* pEchoRet = nullptr; std::atomic<float>* pTape = nullptr; std::atomic<float>* pDust = nullptr;
    std::atomic<float>* pCrush = nullptr; std::atomic<float>* pSource = nullptr;
    struct PadParams { std::atomic<float>* level; std::atomic<float>* tune; std::atomic<float>* decay; std::atomic<float>* pan;
                       std::atomic<float>* space; std::atomic<float>* echo; std::atomic<float>* mute; std::atomic<float>* solo; };
    std::array<PadParams, ru8::kNumPads> pp {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RU8AudioProcessor)
};
