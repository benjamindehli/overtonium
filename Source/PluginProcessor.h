#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginParameters.h"
#include "dsp/SynthEngine.h"

// juce_add_plugin defines this for the plugin targets. The headless integration
// test builds the same sources as a console app, where it is absent.
#ifndef JucePlugin_Name
#define JucePlugin_Name "Overtonium"
#endif

class OvertoniumProcessor : public juce::AudioProcessor,
                            private juce::MPEInstrument::Listener {
public:
  OvertoniumProcessor();
  ~OvertoniumProcessor() override = default;

  void prepareToPlay(double sampleRate,
                     int maximumExpectedSamplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midi) override;

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

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "Default"; }
  void changeProgramName(int, const juce::String &) override {}

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

  /// Declared before the APVTS, which borrows it, so it outlives it.
  ///
  /// Thirty-two channels ganged by LINK means one drag can move six hundred
  /// values at once, and before this the only way back from a drag you did not
  /// mean was to reload the preset. Transactions are opened by the editor once
  /// the tree has been quiet for a moment, so a drag is one step rather than
  /// fifty. See OvertoniumEditor::timerCallback.
  juce::UndoManager undoManager{30 * 1024 * 1024};

  juce::AudioProcessorValueTreeState apvts;

  juce::UndoManager &undo() { return undoManager; }

  /// The same cached atomics the audio thread reads. The editor polls mute and
  /// solo several times a second, and going through the parameter map for that
  /// builds a string per lookup, which is a hundred allocations a tick on the
  /// message thread for values that are already sitting in here.
  const ovt::params::Cache &parameters() const noexcept { return paramCache; }

private:
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
  void notePitchbendChanged(juce::MPENote note) override;
  void noteReleased(juce::MPENote note) override;

  /// Folds the channel-wide expression sources down to the single value the
  /// per-channel AT amounts read, according to what the setting says to
  /// listen to.
  void updateAftertouch();
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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumProcessor)
};
