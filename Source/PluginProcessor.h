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

class OvertoniumProcessor : public juce::AudioProcessor {
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

  /// Polled by the editor to drive the output meter.
  float getOutputLevelLeft() const noexcept {
    return engine.getOutputLevelLeft();
  }

  float getOutputLevelRight() const noexcept {
    return engine.getOutputLevelRight();
  }

  juce::AudioProcessorValueTreeState apvts;

private:
  void handleMidiMessage(const juce::MidiMessage &m);
  void renderSegment(int startSample, int numSamples);

  ovt::params::Cache paramCache;
  ovt::SynthEngine engine;
  ovt::SynthParams currentParams;

  /// The engine always writes here first; the host buffer may be mono, stereo
  /// or wider.
  juce::AudioBuffer<float> scratch;

  float pitchBendNormalised = 0.0f;
  float channelPressure = 0.0f;
  std::atomic<int> activeVoices{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumProcessor)
};
