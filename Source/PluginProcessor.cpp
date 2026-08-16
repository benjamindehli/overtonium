#include "PluginProcessor.h"

OvertoniumProcessor::OvertoniumProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "OVERTONIUM",
            ovt::params::createParameterLayout()) {
  paramCache.connect(apvts);
}

void OvertoniumProcessor::prepareToPlay(double sampleRate,
                                        int maximumExpectedSamplesPerBlock) {
  engine.prepare(sampleRate);
  scratch.setSize(2, juce::jmax(1, maximumExpectedSamplesPerBlock), false, true,
                  true);
  pitchBendNormalised = 0.0f;
  channelPressure = 0.0f;
  modWheel = 0.0f;
}

void OvertoniumProcessor::releaseResources() {
  engine.allSoundOff();
  scratch.setSize(2, 1, false, true, true);
}

bool OvertoniumProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  if (layouts.getMainInputChannels() != 0)
    return false;

  const auto out = layouts.getMainOutputChannelSet();
  return out == juce::AudioChannelSet::mono() ||
         out == juce::AudioChannelSet::stereo();
}

double OvertoniumProcessor::getTailLengthSeconds() const {
  // Report the longest release currently dialled in rather than the worst case
  // the ranges allow, so offline bounces are not padded with 20 s of silence.
  float longest = 0.0f;

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const auto &osc = paramCache.osc[(size_t)i];

    // The key-off stage runs before the release does, so a partial with a long
    // swell outlasts its release time.
    const auto swell = osc.swell != nullptr ? osc.swell->load() : 0.0f;
    const auto release = osc.release != nullptr ? osc.release->load() : 0.0f;

    longest = juce::jmax(longest, swell + release);
  }

  // Whatever is still ringing in the master effects when the last voice ends
  // is part of the tail too, and it can easily outlast the longest release.
  return (double)longest + (double)engine.effectsTailSeconds(currentParams) +
         0.1;
}

void OvertoniumProcessor::updateAftertouch() {
  using ovt::params::AftertouchSource;

  const auto source =
      paramCache.atSource != nullptr
          ? (AftertouchSource)juce::jlimit(
                0, (int)ovt::params::kAftertouchSourceNames.size() - 1,
                (int)std::lround(paramCache.atSource->load()))
          : AftertouchSource::Either;

  switch (source) {
  case AftertouchSource::ChannelPressure:
    currentParams.global.aftertouch = channelPressure;
    break;

  case AftertouchSource::ModWheel:
    currentParams.global.aftertouch = modWheel;
    break;

  default:
    currentParams.global.aftertouch = juce::jmax(channelPressure, modWheel);
    break;
  }
}

void OvertoniumProcessor::handleMidiMessage(const juce::MidiMessage &m) {
  if (m.isNoteOn()) {
    engine.noteOn(m.getNoteNumber(), m.getFloatVelocity(), currentParams);
  } else if (m.isNoteOff()) {
    engine.noteOff(m.getNoteNumber());
  } else if (m.isPitchWheel()) {
    pitchBendNormalised = ((float)m.getPitchWheelValue() - 8192.0f) / 8192.0f;
    currentParams.global.bendSemitones =
        pitchBendNormalised * paramCache.bendRange->load();
  } else if (m.isChannelPressure()) {
    channelPressure = (float)m.getChannelPressureValue() / 127.0f;
    updateAftertouch();
  } else if (m.isController() && m.getControllerNumber() == 1) {
    // Checked by number rather than by one of the isSomething helpers, so it
    // cannot swallow the sustain pedal or the panic messages further down,
    // which are controllers too.
    modWheel = (float)m.getControllerValue() / 127.0f;
    updateAftertouch();
  } else if (m.isAftertouch()) {
    engine.setPolyPressure(m.getNoteNumber(),
                           (float)m.getAfterTouchValue() / 127.0f);
  } else if (m.isSustainPedalOn()) {
    engine.setSustainPedal(true);
  } else if (m.isSustainPedalOff()) {
    engine.setSustainPedal(false);
  } else if (m.isAllNotesOff()) {
    engine.allNotesOff();
  } else if (m.isAllSoundOff()) {
    engine.allSoundOff();
    // The wheel is left alone: it is a physical position the player has set,
    // not something a panic message should silently move.
    channelPressure = 0.0f;
    updateAftertouch();
  }
}

void OvertoniumProcessor::renderSegment(int startSample, int numSamples) {
  if (numSamples <= 0)
    return;

  engine.render(scratch.getWritePointer(0, startSample),
                scratch.getWritePointer(1, startSample), numSamples,
                currentParams);
}

void OvertoniumProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                       juce::MidiBuffer &midi) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();

  if (numSamples <= 0)
    return;

  // Hosts are allowed to exceed the block size they promised in prepareToPlay.
  if (scratch.getNumSamples() < numSamples)
    scratch.setSize(2, numSamples, false, true, true);

  paramCache.snapshot(currentParams, pitchBendNormalised);
  updateAftertouch();
  engine.setPolyphony(paramCache.polyphonyValue());

  // Render in segments split on MIDI timestamps so note timing is sample
  // accurate.
  int position = 0;

  for (const auto metadata : midi) {
    // Clamping against `position` rather than 0 keeps the segments monotonic
    // even if a host hands us out-of-order timestamps; otherwise we would
    // render backwards over audio we had already written.
    const int eventTime =
        juce::jlimit(position, numSamples, metadata.samplePosition);

    renderSegment(position, eventTime - position);
    position = eventTime;

    handleMidiMessage(metadata.getMessage());
  }

  renderSegment(position, numSamples - position);

  // ---- fan the stereo render out to whatever the host asked for -------------
  const int numOut = buffer.getNumChannels();
  const auto *left = scratch.getReadPointer(0);
  const auto *right = scratch.getReadPointer(1);

  if (numOut == 1) {
    auto *dest = buffer.getWritePointer(0);
    for (int n = 0; n < numSamples; ++n)
      dest[n] = 0.5f * (left[n] + right[n]);
  } else if (numOut >= 2) {
    buffer.copyFrom(0, 0, left, numSamples);
    buffer.copyFrom(1, 0, right, numSamples);

    for (int ch = 2; ch < numOut; ++ch)
      buffer.clear(ch, 0, numSamples);
  }

  activeVoices.store(engine.getActiveVoiceCount());
}

void OvertoniumProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto xml = apvts.copyState().createXml())
    copyXmlToBinary(*xml, destData);
}

void OvertoniumProcessor::setStateInformation(const void *data,
                                              int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new OvertoniumProcessor();
}
