#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginParameters.h"
#include "UpdateCheck.h"
#include "dsp/SynthEngine.h"

// juce_add_plugin defines this for the plugin targets. The headless integration
// test builds the same sources as a console app, where it is absent.
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

  /// Overriding one processBlock hides the rest of the overload set, the
  /// double-precision one included. Nothing calls it, since
  /// supportsDoublePrecisionProcessing is left at its default of false, but
  /// hiding a base overload rather than inheriting it is worth not doing:
  /// the day the plugin does support doubles, the host would silently get
  /// the base class's empty implementation instead of a compile error.
  using juce::AudioProcessor::processBlock;

  /// Defined in PluginEditor.cpp so this translation unit stays free of GUI
  /// dependencies.
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }

  bool acceptsMidi() const override { return true; }

  /// Read by the AU, AUv3 and VST2 wrappers. Follows the setting rather than
  /// being always on, so a host that reconfigures its output when it sees this
  /// only does so once MPE has actually been asked for. Nothing in JUCE's VST3
  /// wrapper reads it, and there MPE arrives as ordinary per-channel MIDI,
  /// which is what this parses anyway.
  bool supportsMPE() const override { return mpeIsOn(); }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override;

  // The factory presets, exposed to the host so they appear in its own preset
  // menu rather than only in the plugin's. Logic and GarageBand read this and
  // nothing else, so without it their preset list is a single entry.
  //
  // Names are fixed, hence changeProgramName does nothing. A host that offers
  // to rename a factory preset is offering something this plugin does not do,
  // and quietly ignoring it beats storing a name nothing will ever read back.
  int getNumPrograms() override;
  int getCurrentProgram() override { return currentProgram; }
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String &) override {}

  /// Loads a factory preset and records it as the current program, whether or
  /// not it already was. The plugin's own preset menu goes through here rather
  /// than setCurrentProgram, so choosing the preset you are already on really
  /// does reload it and discard what you changed, which is what picking a
  /// preset by hand is usually for.
  void applyFactoryPreset(int index);

  /// Loads whatever a MIDI program change has asked for since the last call,
  /// and does nothing if none has.
  ///
  /// Driven by the timer this class runs. Public because a test has no message
  /// loop to run that timer, the same reason the docs renderer drives the
  /// editor's timer by hand.
  void applyPendingProgramChange();

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  /// Polled by the editor for the voice-count readout.
  int getActiveVoiceCount() const noexcept { return activeVoices.load(); }

  /// Polled by the editor to drive the per-channel meters.
  float getPartialLevel(int index0) const noexcept {
    return engine.getPartialLevel(index0);
  }

  /// Polled by the editor to drive the noise channel meter.
  float getNoiseLevel() const noexcept { return engine.getNoiseLevel(); }

  /// Polled by the editor to drive the lamps between the knob groups. All
  /// taken from the loudest voice on that partial, the same one the meter
  /// reads. See SynthEngine::sumVoices.
  float getPartialEnvelope(int index0) const noexcept {
    return engine.getPartialEnvelope(index0);
  }

  float getPartialTremolo(int index0) const noexcept {
    return engine.getPartialTremolo(index0);
  }

  float getPartialPitch(int index0) const noexcept {
    return engine.getPartialPitch(index0);
  }

  float getNoiseEnvelope() const noexcept { return engine.getNoiseEnvelope(); }
  float getNoiseTremolo() const noexcept { return engine.getNoiseTremolo(); }

  /// Polled by the editor to drive the output meter.
  float getOutputLevelLeft() const noexcept {
    return engine.getOutputLevelLeft();
  }

  float getOutputLevelRight() const noexcept {
    return engine.getOutputLevelRight();
  }

  /// Declared before the APVTS, which is built beside it.
  ///
  /// Thirty-two channels ganged by LINK means one drag moves thirty-two values
  /// at once, and without a history the only way back from a drag you did not
  /// mean is to reload the preset.
  ///
  /// The value tree is deliberately not given this. A tree that holds an undo
  /// manager records every write to it, and most writes are not somebody
  /// editing: a host playing an automation lane writes constantly, and a
  /// history filling up with a fader somebody else automated three minutes
  /// ago is not a history of anything. What goes in here is what a person
  /// did, and nothing else. See recordEdit.
  juce::UndoManager undoManager{30 * 1024 * 1024};

  juce::AudioProcessorValueTreeState apvts;

  juce::UndoManager &undo() { return undoManager; }

  /// Runs `change` and puts whatever it moved into the history as one step.
  ///
  /// For the things a person does that are not one gesture on one control:
  /// loading a preset writes hundreds of parameters and has to come back in
  /// one undo. Gestures need no help, since a control opens and closes one
  /// around whatever it writes and this is listening. See
  /// audioProcessorParameterChangeGestureBegin.
  ///
  /// The caller decides, which is the point. Loading a preset from the menu
  /// goes through here and a clip firing a program change does not, though
  /// both end up in the same applyFactoryPreset.
  void recordEdit(const juce::String &name,
                  const std::function<void()> &change);

  /// The update check, which lives here rather than with the editor.
  ///
  /// An editor closing must not wait for a network thread. The wait would be
  /// on the message thread, and a fetch is allowed five seconds of silence
  /// before it gives up, so a window closing over a stalled socket would take
  /// the host with it. Owned here, a closing window only asks the fetch to
  /// stop, and the one place that waits is this object going away, which is
  /// the whole plugin being removed rather than a window being shut.
  ovt::UpdateCheck &updates() noexcept { return updateCheck; }

  /// What is loaded, by name, for the editor to show and for the state to
  /// carry.
  ///
  /// Held here rather than on the editor because it has to outlive one. A
  /// window is closed and reopened far more often than a preset is chosen, and
  /// the button read as empty every time until this existed. Empty means
  /// nothing has been loaded, which is what a new instance is.
  const juce::String &presetName() const noexcept { return loadedPresetName; }

  /// For a preset of your own, which has no program index to be found by.
  void setLoadedPresetName(const juce::String &name);

  /// The same cached atomics the audio thread reads. The editor polls mute and
  /// solo several times a second, and going through the parameter map for that
  /// builds a string per lookup, which is a hundred allocations a tick on the
  /// message thread for values that are already sitting in here.
  const ovt::params::Cache &parameters() const noexcept { return paramCache; }

private:
  /// One stereo output, which is the mix.
  ///
  /// A member rather than a free function because BusesProperties is
  /// protected, and static because the constructor needs it before there is
  /// an object.
  static BusesProperties buses();

  void handleMidiMessage(const juce::MidiMessage &m);

  /// Everything that is the same whichever kind of controller is playing:
  /// notes without a channel of their own, the wheel, the pedal, the panics.
  void handleOrdinaryMidiMessage(const juce::MidiMessage &m);

  bool mpeIsOn() const noexcept;

  /// Points the MPE parser at a fresh lower zone, or takes it out of use.
  ///
  /// Called when the setting changes rather than every block, because a
  /// controller is allowed to announce its own bend ranges and rewriting the
  /// layout underneath it would undo that as fast as it arrived.
  void setMpeEnabled(bool on);

  // ---- MPEInstrument::Listener ----------------------------------------------
  void noteAdded(juce::MPENote note) override;
  void notePressureChanged(juce::MPENote note) override;
  void noteTimbreChanged(juce::MPENote note) override;
  void notePitchbendChanged(juce::MPENote note) override;
  void noteReleased(juce::MPENote note) override;

  /// Folds the channel-wide expression sources down to the single value the
  /// per-channel AT amounts read, according to what the setting says to
  /// listen to.
  void updateAftertouch();

  /// Collects what MIDI has asked for and the audio thread cannot do itself.
  /// Program changes, so far.
  void timerCallback() override;

  // ---- juce::AudioProcessorListener ----
  //
  // On itself, so that what a person does to a control can be told apart from
  // what a host does to a parameter. A control opens a gesture before it
  // writes and closes one after, and automation does not.
  void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *,
                                                 int index) override;
  void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *,
                                               int index) override;

  /// Deliberately nothing. A parameter change says what moved and not who
  /// moved it, and it arrives from the audio thread when a host is playing a
  /// lane, which is exactly the case this whole arrangement exists to leave
  /// out of the history.
  void audioProcessorParameterChanged(juce::AudioProcessor *, int,
                                      float) override {}

  /// Likewise. Programs and latency are not edits.
  void audioProcessorChanged(
      juce::AudioProcessor *,
      const juce::AudioProcessorListener::ChangeDetails &) override {}

  /// Where the values stood when the edit being recorded began, normalised
  /// and in parameter order. Empty when nothing is being recorded.
  std::vector<float> editBaseline;

  /// How many gestures are open. The first to open takes the baseline and the
  /// last to close writes the step, so a LINK drag across 32 channels is one
  /// step rather than 32.
  int openGestures = 0;

  /// Set while recordEdit is running its change, so a gesture inside one does
  /// not start a second recording inside the first.
  bool recording = false;

  void beginEdit();
  void endEdit(const juce::String &name);

  /// Renders into the scratch at an offset from its start, not from the start
  /// of the host's block: when a block is cut into pieces the scratch holds
  /// one piece at a time.
  void renderSegment(int scratchOffset, int numSamples);

  ovt::params::Cache paramCache;
  ovt::SynthEngine engine;
  ovt::SynthParams currentParams;

  /// The engine always writes here first; the host buffer may be mono, stereo
  /// or wider.
  juce::AudioBuffer<float> scratch;

  /// Parses the channel layout an MPE controller uses and works out what each
  /// note's bend and pressure add up to, master channel included. The voice
  /// pool is this plugin's own, so only the parsing is borrowed.
  juce::MPEInstrument mpeInstrument{juce::MPEZoneLayout{}};

  /// What the setting was last time it was looked at, so the zone layout is
  /// rebuilt on the change rather than on every block.
  bool mpeWasOn = false;

  float pitchBendNormalised = 0.0f;
  float channelPressure = 0.0f;

  /// CC1. Held rather than consumed, since it is a position the player has
  /// left the wheel in rather than an event.
  float modWheel = 0.0f;
  std::atomic<int> activeVoices{0};

  /// The factory preset the host believes is loaded. Saved with the state so
  /// that restoring a session does not look like a program change, which a
  /// host would answer by loading that preset over the top of everything the
  /// session just restored.
  int currentProgram = 0;

  /// The factory preset a MIDI program change has asked for and nobody has
  /// loaded yet, or -1 for none.
  ///
  /// The audio thread writes it and the timer reads it, which is the whole
  /// point of it being a number in a box rather than a preset load: applying
  /// one walks 781 parameters and tells the host about each one, allocating
  /// and taking locks on the way, and none of that belongs on the audio
  /// thread. The cost is that the preset lands at the next tick rather than at
  /// the message's timestamp, which nothing can hear: a preset change is
  /// hundreds of parameter moves and was never a sample-accurate event.
  ///
  /// A second program change before the first has been read replaces it. Two
  /// of them inside one tick means the first was never meant to be heard.
  std::atomic<int> pendingProgram{-1};

  /// Whether currentProgram describes something that was actually loaded.
  ///
  /// It has to report a number from the moment the plugin exists, and zero is
  /// the only honest one to pick, but a new instance sounds like the parameter
  /// defaults rather than like the first preset. Without this, a host's menu
  /// showed that preset selected and picking it was a no-op. See
  /// setCurrentProgram.
  bool programApplied = false;

  /// The name shown on the preset button. See presetName().
  juce::String loadedPresetName;

  /// Last, so that it is the first thing destroyed. Its destructor joins a
  /// network thread, and doing that before the rest of the instance goes means
  /// there is nothing left running while the engine and the buffers are torn
  /// down. See `updates()` for why the editor does not own this.
  ovt::UpdateCheck updateCheck;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumProcessor)
};
