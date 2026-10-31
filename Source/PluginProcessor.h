#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "MidiLearn.h"
#include "PluginParameters.h"
#include "UpdateCheck.h"
#include "dsp/SynthEngine.h"

// juce_add_plugin defines this for the plugin targets. The headless
// integration test builds the same sources as a console app without it.
#ifndef JucePlugin_Name
#define JucePlugin_Name "Overtonium"
#endif

class OvertoniumProcessor : public juce::AudioProcessor,
                            private juce::MPEInstrument::Listener,
                            private juce::Timer,
                            private juce::AudioProcessorListener {
public:
  OvertoniumProcessor();
  ~OvertoniumProcessor() override;

  void prepareToPlay(double sampleRate,
                     int maximumExpectedSamplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midi) override;

  /// Keeps the base overloads visible, so a double-precision call reaches the
  /// base class rather than being hidden.
  using juce::AudioProcessor::processBlock;

  /// Defined in PluginEditor.cpp so this translation unit stays free of GUI
  /// dependencies.
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }

  bool acceptsMidi() const override { return true; }

  /// Read by the AU, AUv3 and VST2 wrappers. Follows the MPE setting, so a
  /// host only reconfigures for MPE once it has been asked for.
  bool supportsMPE() const override { return mpeIsOn(); }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override;

  // The factory presets, exposed to the host's own preset menu. Names are
  // fixed, so changeProgramName does nothing.
  int getNumPrograms() override;
  int getCurrentProgram() override { return currentProgram.load(); }
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String &) override {}

  /// Loads a factory preset and records it as the current program, even if it
  /// already was, so choosing the current preset from the menu reloads it.
  void applyFactoryPreset(int index);

  /// Loads whatever a MIDI program change has asked for since the last call.
  /// Driven by this class's timer, and public so a test can call it without a
  /// message loop.
  void applyPendingProgramChange();

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  /// Polled by the editor for the voice-count readout.
  int getActiveVoiceCount() const noexcept { return activeVoices.load(); }

  /// Polled by the editor for the Playing bar. Each is published at the end of
  /// every block.
  ///
  /// Whether a sustain pedal is down on any channel.
  bool isSustainDown() const noexcept { return sustainShown.load(); }

  /// The pitch wheel, or the widest bend on any key held under MPE, from -1 to
  /// 1 against the BEND range.
  float getBendPosition() const noexcept { return bendShown.load(); }

  /// The mod wheel, 0 to 1.
  float getWheelPosition() const noexcept { return wheelShown.load(); }

  /// Channel pressure, or the strongest pressure on any held key, whichever is
  /// higher, 0 to 1. What the keyboard sends, not what Aftertouch from passes
  /// on.
  float getPressurePosition() const noexcept { return pressureShown.load(); }

  /// Polled by the editor to drive the per-channel meters.
  float getPartialLevel(int index0) const noexcept {
    return engine.getPartialLevel(index0);
  }

  /// Polled by the editor to drive the noise channel meter.
  float getNoiseLevel() const noexcept { return engine.getNoiseLevel(); }

  /// Polled by the editor to drive the lamps between the knob groups. Taken
  /// from the loudest voice on that partial, as the meter is.
  float getPartialEnvelope(int index0) const noexcept {
    return engine.getPartialEnvelope(index0);
  }

  float getPartialTremolo(int index0) const noexcept {
    return engine.getPartialTremolo(index0);
  }

  /// The held chord as the Adaptive temperament last settled it, for the
  /// Tuning bar's readouts. Returns true and fills @p out when it has been
  /// settled again since the last call. Lock free. From the message thread
  /// only.
  bool readAdaptiveSnapshot(ovt::AdaptiveSnapshot &out) noexcept {
    return engine.readAdaptiveSnapshot(out);
  }

  float getPartialPitch(int index0) const noexcept {
    return engine.getPartialPitch(index0);
  }

  float getPartialVelocity(int index0) const noexcept {
    return engine.getPartialVelocity(index0);
  }

  float getPartialPressure(int index0) const noexcept {
    return engine.getPartialPressure(index0);
  }

  float getNoiseEnvelope() const noexcept { return engine.getNoiseEnvelope(); }
  float getNoiseTremolo() const noexcept { return engine.getNoiseTremolo(); }
  float getNoiseVelocity() const noexcept { return engine.getNoiseVelocity(); }
  float getNoisePressure() const noexcept { return engine.getNoisePressure(); }

  /// Polled by the editor to drive the output meter.
  float getOutputLevelLeft() const noexcept {
    return engine.getOutputLevelLeft();
  }

  float getOutputLevelRight() const noexcept {
    return engine.getOutputLevelRight();
  }

  /// The edit history. Declared before the APVTS, which is built beside it.
  ///
  /// Not given to the value tree, since a tree with an undo manager records
  /// every write, automation included. Only what a person did goes in here.
  /// See recordEdit.
  juce::UndoManager undoManager{30 * 1024 * 1024};

  juce::AudioProcessorValueTreeState apvts;

  juce::UndoManager &undo() { return undoManager; }

  /// Runs `change` and puts whatever it moved into the history as one step.
  ///
  /// For edits that are not one gesture on one control, such as loading a
  /// preset. Gestures are recorded without this, see
  /// audioProcessorParameterChangeGestureBegin. A program change from MIDI does
  /// not go through here, so it is not recorded.
  void recordEdit(const juce::String &name,
                  const std::function<void()> &change);

  /// The update check. Owned here rather than by the editor so a closing
  /// window only asks the fetch to stop and never waits on the network.
  ovt::UpdateCheck &updates() noexcept { return updateCheck; }

  /// Which controller moves which control. Edited from the editor's menus and
  /// used here as messages arrive. See ovt::MidiLearn for the thread rules.
  ovt::MidiLearn midiLearn;

  /// The name of what is loaded, for the editor to show and the state to
  /// carry. Held here so it outlives the editor. Empty when nothing has been
  /// loaded.
  juce::String presetName() const {
    // By value, under the lock, since a host thread restoring a session may
    // write this while the window reads it.
    const juce::ScopedLock sl(sessionLock);
    return loadedPresetName;
  }

  /// For a preset of your own, which has no program index to be found by.
  void setLoadedPresetName(const juce::String &name);

  /// What every direct write to apvts.state has to hold, and what the host's
  /// state calls hold while they read or replace it.
  ///
  /// The editor writes non-parameter properties onto the tree from the message
  /// thread, and a host may call getStateInformation from any thread.
  /// juce::ValueTree is not thread safe, and the APVTS lock does not cover
  /// setProperty from outside. Not used by the audio thread.
  juce::CriticalSection &stateLock() noexcept { return sessionLock; }

  /// The same cached atomics the audio thread reads, so the editor can poll
  /// values without the string lookup the parameter map needs.
  const ovt::params::Cache &parameters() const noexcept { return paramCache; }

  /// The channels each macro on Selected reaches. Not in the parameter tree.
  /// Written by the macro panel and by a user preset, saved with the session,
  /// and cleared by a factory preset.
  ovt::params::MacroChannels &macroChannels() noexcept {
    return paramCache.macroChannels;
  }

private:
  /// One stereo output. Static because the constructor needs it before there
  /// is an object.
  static BusesProperties buses();

  void handleMidiMessage(const juce::MidiMessage &m);

  /// MIDI handling shared by MPE and ordinary controllers: notes without a
  /// channel of their own, the wheel, the pedal, the panics.
  void handleOrdinaryMidiMessage(const juce::MidiMessage &m);

  bool mpeIsOn() const noexcept;

  /// Points the MPE parser at a fresh lower zone, or takes it out of use.
  /// Called only when the setting changes, so a controller's own bend range
  /// announcements are not overwritten.
  void setMpeEnabled(bool on);

  // ---- MPEInstrument::Listener ----------------------------------------------
  void noteAdded(juce::MPENote note) override;
  void notePressureChanged(juce::MPENote note) override;
  void noteTimbreChanged(juce::MPENote note) override;
  void notePitchbendChanged(juce::MPENote note) override;
  void noteReleased(juce::MPENote note) override;

  /// Folds the channel-wide expression sources into the single value the
  /// per-channel AT amounts read, as the setting says.
  void updateAftertouch();

  /// Handles what the audio thread cannot: program changes from MIDI, and
  /// keeping the reported latency in step with the lookahead switch.
  void timerCallback() override;

  /// The latency the host should be told at the current rate and settings.
  int latencyAtThisRate() const;

  /// Tells the host the latency if it has changed. Message thread only.
  void reportLatency();

  /// What prepareToPlay was last given.
  double hostRate = 48000.0;

  // ---- juce::AudioProcessorListener ----
  //
  // Listening to itself tells a person's edits from automation: a control
  // opens a gesture before it writes and closes one after, automation does not.
  void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *,
                                                 int index) override;
  void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *,
                                               int index) override;

  /// Deliberately nothing. A parameter change does not say who moved it, and
  /// automation must stay out of the history.
  void audioProcessorParameterChanged(juce::AudioProcessor *, int,
                                      float) override {}

  /// Likewise. Programs and latency are not edits.
  void audioProcessorChanged(
      juce::AudioProcessor *,
      const juce::AudioProcessorListener::ChangeDetails &) override {}

  /// Normalised values in parameter order when the edit being recorded began.
  /// Empty when nothing is being recorded.
  std::vector<float> editBaseline;

  /// Where the channels of the macros on Selected stood, likewise.
  std::array<std::uint32_t, ovt::params::kNumMacros> masksBaseline{};

  /// How many gestures are open. The first takes the baseline and the last
  /// writes the step, so a LINK drag is one step.
  int openGestures = 0;

  /// Set while recordEdit runs its change, so a gesture inside it does not
  /// start a second recording.
  bool recording = false;

  void beginEdit();
  void endEdit(const juce::String &name);

  /// Renders into the scratch at an offset from the scratch start, since a
  /// split host block is rendered one piece at a time.
  void renderSegment(int scratchOffset, int numSamples);

  ovt::params::Cache paramCache;
  ovt::SynthEngine engine;
  ovt::SynthParams currentParams;

  /// The engine always writes here first. The host buffer may be mono, stereo
  /// or wider.
  juce::AudioBuffer<float> scratch;

  /// Parses the MPE channel layout and each note's bend and pressure. The voice
  /// pool is this plugin's own.
  juce::MPEInstrument mpeInstrument{juce::MPEZoneLayout{}};

  /// The setting when last checked, so the zone layout is rebuilt only on a
  /// change.
  bool mpeWasOn = false;

  float pitchBendNormalised = 0.0f;
  float channelPressure = 0.0f;

  /// CC1, held as a position rather than consumed as an event.
  float modWheel = 0.0f;
  std::atomic<int> activeVoices{0};

  /// One bit per MIDI channel whose sustain pedal is down. Tracked here because
  /// with MPE on the parser takes the pedal and the engine never sees it.
  uint32_t pedalChannels = 0;

  void followPedal(const juce::MidiMessage &) noexcept;
  void publishPlaying() noexcept;

  std::atomic<bool> sustainShown{false};
  std::atomic<float> bendShown{0.0f}, wheelShown{0.0f}, pressureShown{0.0f};

  /// The factory preset the host believes is loaded. Saved with the state so
  /// restoring a session does not look like a program change.
  ///
  /// Atomic because a host may save state on one thread while restoring on
  /// another.
  std::atomic<int> currentProgram{0};

  /// The factory preset a MIDI program change asked for and nobody has loaded
  /// yet, or -1.
  ///
  /// Written by the audio thread and read by the timer, since loading a preset
  /// allocates and locks and must not happen on the audio thread. A newer
  /// program change replaces an unread one.
  std::atomic<int> pendingProgram{-1};

  /// Whether currentProgram describes something that was actually loaded. A
  /// new instance reports 0 but sounds like the defaults, so picking preset 0
  /// must still load it. See setCurrentProgram.
  std::atomic<bool> programApplied{false};

  /// The name shown on the preset button. Guarded by stateLock, since a
  /// juce::String cannot be atomic.
  juce::String loadedPresetName;

  /// See stateLock. Mutable so presetName can take it.
  mutable juce::CriticalSection sessionLock;

  /// Last, so it is destroyed first and its network thread is joined before
  /// the rest of the instance is torn down.
  ovt::UpdateCheck updateCheck;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumProcessor)
};
