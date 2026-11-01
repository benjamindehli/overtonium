#include "PluginProcessor.h"
#include "Presets.h"

#include <utility>

namespace {
/// One step in the history: the normalised values of everything that moved
/// in one edit, and the channels of the macros on Selected, which a preset
/// load also sets.
class ParameterEdit final : public juce::UndoableAction {
public:
  struct Change {
    juce::RangedAudioParameter *param;
    float before, after;
  };

  using Masks = std::array<std::uint32_t, ovt::params::kNumMacros>;

  ParameterEdit(std::vector<Change> movedIn, juce::String nameIn,
                ovt::params::MacroChannels &channelsIn, Masks beforeIn,
                Masks afterIn)
      : moved(std::move(movedIn)), name(std::move(nameIn)),
        channels(channelsIn), masksBefore(beforeIn), masksAfter(afterIn) {}

  /// Nothing the first time, since a step is pushed after its change has
  /// already happened. A redo writes the values.
  bool perform() override {
    if (std::exchange(pushed, true)) {
      setMasks(masksAfter);
      return apply(&Change::after);
    }

    return true;
  }

  bool undo() override {
    setMasks(masksBefore);
    return apply(&Change::before);
  }

  int getSizeInUnits() override { return (int)(moved.size() * sizeof(Change)); }

private:
  void setMasks(const Masks &masks) {
    for (size_t m = 0; m < masks.size(); ++m)
      channels[m].store(masks[m]);
  }

  bool apply(float Change::*which) {
    for (const auto &change : moved)
      if (change.param != nullptr)
        change.param->setValueNotifyingHost(change.*which);

    return true;
  }

  std::vector<Change> moved;
  juce::String name;
  ovt::params::MacroChannels &channels;
  Masks masksBefore, masksAfter;
  bool pushed = false;
};

ParameterEdit::Masks masksOf(const ovt::params::MacroChannels &channels) {
  ParameterEdit::Masks out{};

  for (size_t m = 0; m < out.size(); ++m)
    out[m] = channels[m].load();

  return out;
}
} // namespace

juce::AudioProcessor::BusesProperties OvertoniumProcessor::buses() {
  return BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(),
                                      true);
}

OvertoniumProcessor::OvertoniumProcessor()
    : juce::AudioProcessor(buses()),
      apvts(*this, nullptr, "OVERTONIUM",
            ovt::params::createParameterLayout()) {
  paramCache.connect(apvts);
  mpeInstrument.addListener(this);

  // Listens to itself to tell a person's edits from automation. See
  // audioProcessorParameterChangeGestureBegin.
  addListener(this);

  // Only carries program changes from MIDI, so it can run slowly and keeps
  // running with no window open.
  startTimerHz(20);
}

OvertoniumProcessor::~OvertoniumProcessor() {
  // Before anything else goes. Timer is a base class and is destroyed last,
  // so a tick during teardown would call into a half destroyed object. The
  // same goes for the listener.
  stopTimer();
  removeListener(this);
}

// -----------------------------------------------------------------------------
// The undo history: what a person did, and nothing else.

void OvertoniumProcessor::beginEdit() {
  editBaseline.clear();
  editBaseline.reserve((size_t)getParameters().size());

  for (const auto *param : getParameters())
    editBaseline.push_back(param->getValue());

  masksBaseline = masksOf(paramCache.macroChannels);
}

void OvertoniumProcessor::endEdit(const juce::String &name) {
  const auto &params = getParameters();

  if (editBaseline.size() != (size_t)params.size()) {
    editBaseline.clear();
    return;
  }

  std::vector<ParameterEdit::Change> moved;

  for (int i = 0; i < params.size(); ++i) {
    const auto before = editBaseline[(size_t)i];
    const auto after = params[i]->getValue();

    // Exactly, not within a tolerance: these are stored values, so a parameter
    // moved and moved back holds its original bits. Two comparisons so a
    // float-equality warning does not fire.
    if (!(before < after) && !(after < before))
      continue;

    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(params[i]))
      moved.push_back({ranged, before, after});
  }

  editBaseline.clear();

  const auto masks = masksOf(paramCache.macroChannels);

  if (moved.empty() && masks == masksBaseline)
    return;

  // Its own transaction, so one gesture is one step.
  undoManager.beginNewTransaction(name);
  undoManager.perform(new ParameterEdit(
      std::move(moved), name, paramCache.macroChannels, masksBaseline, masks));
}

void OvertoniumProcessor::recordEdit(const juce::String &name,
                                     const std::function<void()> &change) {
  if (change == nullptr)
    return;

  // A nested recording would take its own baseline, so the outer one records
  // everything.
  if (recording || openGestures > 0) {
    change();
    return;
  }

  const juce::ScopedValueSetter<bool> guard(recording, true);

  beginEdit();
  change();
  endEdit(name);
}

void OvertoniumProcessor::audioProcessorParameterChangeGestureBegin(
    juce::AudioProcessor *, int) {
  // Only on the message thread. A host may open a gesture from the audio
  // thread, and taking a baseline there would allocate.
  if (recording || !juce::MessageManager::existsAndIsCurrentThread())
    return;

  if (openGestures++ == 0)
    beginEdit();
}

void OvertoniumProcessor::audioProcessorParameterChangeGestureEnd(
    juce::AudioProcessor *, int) {
  if (recording || !juce::MessageManager::existsAndIsCurrentThread())
    return;

  if (openGestures > 0 && --openGestures == 0)
    endEdit("Move a control");
}

void OvertoniumProcessor::prepareToPlay(double sampleRate,
                                        int maximumExpectedSamplesPerBlock) {
  engine.prepare(sampleRate);

  // Two stages add latency: the bus stage's oversampling filters and the
  // output stage's limiter lookahead. Both delay by the same amount whatever
  // the patch, so choosing a preset does not change the reported latency.
  // Only the Settings lookahead switch does. See BusDrive::kLatency,
  // OutputStage::kLookaheadSeconds and reportLatency.
  hostRate = sampleRate;
  setLatencySamples(latencyAtThisRate());

  // A floor under the host's block size, so a host that sends a larger block
  // than promised is not cut into many pieces.
  scratch.setSize(2, juce::jmax(512, maximumExpectedSamplesPerBlock), false,
                  true, true);
  pitchBendNormalised = 0.0f;
  channelPressure = 0.0f;
  modWheel = 0.0f;
  pedalChannels = 0;
}

bool OvertoniumProcessor::mpeIsOn() const noexcept {
  return paramCache.mpe != nullptr && paramCache.mpe->load() > 0.5f;
}

void OvertoniumProcessor::setMpeEnabled(bool on) {
  mpeWasOn = on;

  // Notes started by the other set of entry points can no longer be reached
  // and would hang.
  mpeInstrument.releaseAllNotes();
  engine.allNotesOff();
  pedalChannels = 0;

  if (!on) {
    mpeInstrument.setZoneLayout({});
    return;
  }

  // A lower zone using every remaining channel, which a controller's own
  // layout messages may replace. The master bend range follows the BEND knob,
  // and the per-note range is the 48 semitones the specification asks for.
  const auto masterRange =
      paramCache.bendRange != nullptr
          ? juce::jlimit(1, 96, (int)std::lround(paramCache.bendRange->load()))
          : 2;

  juce::MPEZoneLayout layout;
  layout.setLowerZone(15, 48, masterRange);
  mpeInstrument.setZoneLayout(layout);

  // From here the wheel is folded into each note's bend and pressure arrives
  // per note, so the channel-wide values are reset to avoid applying them
  // twice.
  pitchBendNormalised = 0.0f;
  channelPressure = 0.0f;
}

// -----------------------------------------------------------------------------
// The notes an MPE controller sends. Each owns a channel that carries its
// bend and pressure.

void OvertoniumProcessor::noteAdded(juce::MPENote note) {
  engine.noteOnPerNote(note.midiChannel, note.initialNote,
                       note.noteOnVelocity.asUnsignedFloat(), currentParams);

  // A note can arrive already bent and pressed, so both are applied now to
  // avoid a jump.
  notePitchbendChanged(note);
  notePressureChanged(note);

  // The slide is not applied here. Before any CC74 the timbre is JUCE's
  // centre value, not the controller's, and using it as the rest position
  // would make a controller that rests at one end lurch. See Voice::setSlide.
}

void OvertoniumProcessor::notePressureChanged(juce::MPENote note) {
  engine.setNotePressure(note.midiChannel, note.initialNote,
                         note.pressure.asUnsignedFloat());
}

void OvertoniumProcessor::noteTimbreChanged(juce::MPENote note) {
  // Bipolar around the centre, where JUCE starts a note's timbre.
  engine.setNoteSlide(note.midiChannel, note.initialNote,
                      note.timbre.asUnsignedFloat() * 2.0f - 1.0f);
}

void OvertoniumProcessor::notePitchbendChanged(juce::MPENote note) {
  // The sum of the note's own bend and the master channel's.
  engine.setNoteBend(note.midiChannel, note.initialNote,
                     (float)note.totalPitchbendInSemitones);
}

void OvertoniumProcessor::noteReleased(juce::MPENote note) {
  engine.noteOffPerNote(
      note.midiChannel, note.initialNote,
      ovt::liftFromVelocity(note.noteOffVelocity.as7BitInt()));
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

  if (out != juce::AudioChannelSet::mono() &&
      out != juce::AudioChannelSet::stereo())
    return false;

  return true;
}

double OvertoniumProcessor::getTailLengthSeconds() const {
  // The longest release currently dialled in rather than the worst case the
  // ranges allow, so offline bounces are not padded with silence.
  float longest = 0.0f;

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const auto &osc = paramCache.osc[(size_t)i];

    // The key-off swell runs before the release.
    const auto swell = osc.swell != nullptr ? osc.swell->load() : 0.0f;
    const auto release = osc.release != nullptr ? osc.release->load() : 0.0f;

    longest = juce::jmax(longest, swell + release);
  }

  // Plus whatever is still ringing in the master effects.
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

  case AftertouchSource::Either:
    currentParams.global.aftertouch = juce::jmax(channelPressure, modWheel);
    break;
  }
}

void OvertoniumProcessor::handleMidiMessage(const juce::MidiMessage &m) {
  // Before the MPE parser, which would otherwise swallow a bound controller.
  // Safe because MidiLearn refuses the numbers the parser needs. See
  // MidiLearn::isReserved.
  if (m.isController() &&
      midiLearn.handle(m.getControllerNumber(), m.getControllerValue()))
    return;

  followPedal(m);

  if (mpeWasOn) {
    // With MPE on, the parser takes notes, bend, pressure, the pedal and the
    // layout messages. An ordinary keyboard on the master channel still plays.
    mpeInstrument.processNextMidiEvent(m);

    // What the parser leaves alone: the mod wheel, all-sound-off (CC 120) and
    // program changes. All-sound-off also releases the parser's notes.
    if (m.isAllSoundOff())
      mpeInstrument.releaseAllNotes(); // or it goes on holding notes that are
                                       // already gone from the pool

    // An MPE controller sends program changes on a member channel too.
    const bool leftOver = (m.isController() && m.getControllerNumber() == 1) ||
                          m.isAllSoundOff() || m.isProgramChange();

    if (!leftOver)
      return;
  }

  handleOrdinaryMidiMessage(m);
}

void OvertoniumProcessor::followPedal(const juce::MidiMessage &m) noexcept {
  const auto bit = 1u << (unsigned)juce::jlimit(0, 31, m.getChannel());

  if (m.isSustainPedalOn())
    pedalChannels |= bit;
  else if (m.isSustainPedalOff())
    pedalChannels &= ~bit;
  else if (m.isAllNotesOff() || m.isAllSoundOff())
    pedalChannels = 0;

  // The engine reads the pedal for the strings' dampers, and under MPE would
  // otherwise never hear it.
  currentParams.pedalDown = pedalChannels != 0;
}

void OvertoniumProcessor::publishPlaying() noexcept {
  const auto held = engine.heldExpression();
  const auto range = paramCache.bendRange != nullptr
                         ? juce::jmax(1.0f, paramCache.bendRange->load())
                         : 2.0f;

  // The meter shows the furthest bend, whether from the wheel or one MPE note.
  auto bend = pitchBendNormalised;
  if (const auto perNote = held.bendSemitones / range;
      std::abs(perNote) > std::abs(bend))
    bend = perNote;

  sustainShown.store(pedalChannels != 0, std::memory_order_relaxed);
  bendShown.store(juce::jlimit(-1.0f, 1.0f, bend), std::memory_order_relaxed);
  wheelShown.store(modWheel, std::memory_order_relaxed);
  // What the keyboard sends, before Aftertouch from is applied.
  pressureShown.store(
      juce::jlimit(0.0f, 1.0f, juce::jmax(channelPressure, held.pressure)),
      std::memory_order_relaxed);
}

void OvertoniumProcessor::handleOrdinaryMidiMessage(
    const juce::MidiMessage &m) {
  if (m.isNoteOn()) {
    engine.noteOn(m.getNoteNumber(), m.getFloatVelocity(), currentParams);
  } else if (m.isNoteOff()) {
    // The release velocity, or zero for a note-on of velocity zero, which
    // liftFromVelocity reads as neutral.
    engine.noteOff(m.getNoteNumber(), ovt::liftFromVelocity(m.getVelocity()));
  } else if (m.isPitchWheel()) {
    pitchBendNormalised = ((float)m.getPitchWheelValue() - 8192.0f) / 8192.0f;
    currentParams.global.bendSemitones =
        pitchBendNormalised * paramCache.bendRange->load();
  } else if (m.isChannelPressure()) {
    channelPressure = (float)m.getChannelPressureValue() / 127.0f;
    updateAftertouch();
  } else if (m.isController() && m.getControllerNumber() == 1) {
    // Checked by number so it cannot catch the sustain pedal or the panic
    // messages, which are controllers too.
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
    // The mod wheel is left alone, since it is a physical position.
    channelPressure = 0.0f;
    updateAftertouch();
  } else if (m.isProgramChange()) {
    // Stored for the timer to load, since the audio thread must not load a
    // preset. Range checked where the preset list is read.
    pendingProgram.store(m.getProgramChangeNumber(), std::memory_order_relaxed);
  }
}

void OvertoniumProcessor::timerCallback() {
  applyPendingProgramChange();
  reportLatency();
}

int OvertoniumProcessor::latencyAtThisRate() const {
  const auto *ahead = apvts.getRawParameterValue(ovt::params::lookaheadId);

  return ovt::BusDrive::kLatency +
         (ahead == nullptr || ahead->load() > 0.5f
              ? ovt::OutputStage::lookaheadSamples(hostRate)
              : 0);
}

void OvertoniumProcessor::reportLatency() {
  // Polled on the timer rather than from a parameter listener, which could
  // fire on the audio thread, and telling a host its latency changed from there
  // can deadlock it. setLatencySamples only notifies the host on a change.
  const auto wanted = latencyAtThisRate();

  if (wanted != getLatencySamples())
    setLatencySamples(wanted);
}

void OvertoniumProcessor::applyPendingProgramChange() {
  const int index = pendingProgram.exchange(-1, std::memory_order_relaxed);

  if (index < 0)
    return;

  // Through applyFactoryPreset rather than setCurrentProgram, so a program
  // change reloads the preset even when it is already current, and the host's
  // preset menu follows.
  applyFactoryPreset(index);
}

void OvertoniumProcessor::renderSegment(int scratchOffset, int numSamples) {
  if (numSamples <= 0)
    return;

  engine.render(scratch.getWritePointer(0, scratchOffset),
                scratch.getWritePointer(1, scratchOffset), numSamples,
                currentParams);
}

void OvertoniumProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                       juce::MidiBuffer &midi) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();

  if (numSamples <= 0)
    return;

  if (const auto on = mpeIsOn(); on != mpeWasOn)
    setMpeEnabled(on);

  paramCache.snapshot(currentParams, pitchBendNormalised);
  currentParams.pedalDown = pedalChannels != 0;
  updateAftertouch();
  engine.setPolyphony(paramCache.polyphonyValue());
  engine.setLegato(paramCache.legatoValue());

  // A host may send a bigger block than promised. Growing the scratch would
  // allocate on the audio thread, so the block is rendered in pieces instead.
  const auto capacity = juce::jmax(1, scratch.getNumSamples());

  auto event = midi.begin();
  const auto noMoreEvents = midi.end();

  for (int done = 0; done < numSamples;) {
    const auto length = juce::jmin(capacity, numSamples - done);
    const auto chunkEnd = done + length;
    const bool lastChunk = chunkEnd >= numSamples;

    // Render in segments split on MIDI timestamps so note timing is sample
    // accurate.
    auto position = done;

    while (event != noMoreEvents) {
      const auto metadata = *event;
      const auto at = juce::jlimit(0, numSamples, metadata.samplePosition);

      // Events past this piece wait for the next one, except on the last piece.
      if (at >= chunkEnd && !lastChunk)
        break;

      // Clamped against `position` so out-of-order timestamps never render
      // backwards.
      const auto eventTime = juce::jlimit(position, chunkEnd, at);

      renderSegment(position - done, eventTime - position);
      position = eventTime;

      handleMidiMessage(metadata.getMessage());
      ++event;
    }

    renderSegment(position - done, chunkEnd - position);

    // ---- write the mix out --------------------------------------------------
    //
    // Channel indices are bounds-checked here rather than taken from
    // getBusBuffer, which can point past the end of a buffer a host made too
    // short.
    const int available = buffer.getNumChannels();
    const auto *left = scratch.getReadPointer(0);
    const auto *right = scratch.getReadPointer(1);

    const auto channelOf = [this, available](int channel) {
      const int index = getChannelIndexInProcessBlockBuffer(false, 0, channel);
      return index >= 0 && index < available ? index : -1;
    };

    const int mainChannels = juce::jmax(0, getChannelCountOfBus(false, 0));

    if (mainChannels == 1) {
      if (const int ch = channelOf(0); ch >= 0) {
        auto *dest = buffer.getWritePointer(ch, done);
        for (int n = 0; n < length; ++n)
          dest[n] = 0.5f * (left[n] + right[n]);
      }
    } else if (mainChannels >= 2) {
      if (const int ch = channelOf(0); ch >= 0)
        buffer.copyFrom(ch, done, left, length);

      if (const int ch = channelOf(1); ch >= 0)
        buffer.copyFrom(ch, done, right, length);
    }

    done = chunkEnd;
  }

  activeVoices.store(engine.getActiveVoiceCount());
  publishPlaying();
}

int OvertoniumProcessor::getNumPrograms() {
  // More than one program on the Audio Unit only, where Logic and GarageBand
  // read factory presets this way. On VST3 a program count above one makes
  // JUCE publish an automatable Program parameter, which would change the
  // parameter set of released versions and fail pluginval's state test.
  return wrapperType == wrapperType_AudioUnit ? ovt::presets::names().size()
                                              : 1;
}

const juce::String OvertoniumProcessor::getProgramName(int index) {
  const auto all = ovt::presets::names();

  // A host may ask about a cached index that no longer exists.
  return juce::isPositiveAndBelow(index, all.size()) ? all[index]
                                                     : juce::String();
}

void OvertoniumProcessor::setCurrentProgram(int index) {
  // Hosts call this on their own, including while restoring a session with
  // the user's edits to that preset, so the same index does nothing. The
  // exception is a new instance, which reports program 0 but has not loaded
  // it.
  if (index == currentProgram && programApplied)
    return;

  applyFactoryPreset(index);
}

void OvertoniumProcessor::applyFactoryPreset(int index) {
  // Against the number of presets rather than getNumPrograms, which is 1
  // outside the Audio Unit.
  if (!juce::isPositiveAndBelow(index, ovt::presets::names().size()))
    return;

  currentProgram = index;
  programApplied = true;

  {
    // Scoped tightly so the lock is not held across the preset load. See
    // stateLock.
    const juce::ScopedLock sl(sessionLock);
    loadedPresetName = ovt::presets::names()[index];
  }

  ovt::presets::apply(apvts, index);

  // A factory preset puts no macro on Selected, so their channels are cleared.
  for (auto &mask : paramCache.macroChannels)
    mask.store(0);

  // So the host's preset display follows a change made from the plugin's menu.
  updateHostDisplay(
      juce::AudioProcessorListener::ChangeDetails{}.withProgramChanged(true));
}

void OvertoniumProcessor::setLoadedPresetName(const juce::String &name) {
  {
    const juce::ScopedLock sl(sessionLock);
    loadedPresetName = name;
  }

  // A user preset is not a factory program, so a repeat program change from
  // the host must not replace it.
  programApplied = true;
}

/// The property the current program travels under, named so it cannot
/// collide with a parameter id in the same tree.
static const juce::Identifier kCurrentProgramProperty{"overtoniumProgram"};

/// The loaded preset's name, saved separately since a user preset has no
/// program index.
static const juce::Identifier kPresetNameProperty{"overtoniumPresetName"};

/// Where a macro on Selected keeps its channels in the session, one property
/// per macro holding the harmonic numbers, as the LINK selection is kept.
static juce::Identifier macroChannelsProperty(int macro) {
  return juce::Identifier("overtoniumMacro" + juce::String(macro + 1) +
                          "Channels");
}

void OvertoniumProcessor::getStateInformation(juce::MemoryBlock &destData) {
  // Held across the copy and the two session properties only. The editor
  // writes onto this tree from the message thread and a host may call this
  // from any thread. See stateLock.
  auto state = [this] {
    const juce::ScopedLock sl(sessionLock);

    auto copy = apvts.copyState();
    copy.setProperty(kCurrentProgramProperty, currentProgram.load(), nullptr);
    copy.setProperty(kPresetNameProperty, loadedPresetName, nullptr);
    return copy;
  }();

  // Cleared when empty, since a restored tree may still carry an old list.
  for (int m = 0; m < ovt::params::kNumMacros; ++m) {
    const auto mask = paramCache.macroChannels[(size_t)m].load();

    if (mask != 0)
      state.setProperty(macroChannelsProperty(m),
                        ovt::params::channelListText(mask), nullptr);
    else
      state.removeProperty(macroChannelsProperty(m), nullptr);
  }

  // MIDI Learn bindings are saved with the session, not with a preset, since
  // they describe the user's hardware.
  state.removeChild(state.getChildWithName(ovt::MidiLearn::kTreeType), nullptr);
  state.appendChild(midiLearn.toTree(), nullptr);

  if (auto xml = state.createXml())
    copyXmlToBinary(*xml, destData);
}

void OvertoniumProcessor::setStateInformation(const void *data,
                                              int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
    if (xml->hasTagName(apvts.state.getType())) {
      const auto tree = juce::ValueTree::fromXml(*xml);

      // Read before replaceState, so the host's own program restore is a no-op.
      // Defaults to the first preset when absent.
      const int saved = tree.getProperty(kCurrentProgramProperty, 0);

      {
        // Everything here that getStateInformation reads, changed under the
        // same lock. See stateLock.
        const juce::ScopedLock sl(sessionLock);

        loadedPresetName =
            tree.getProperty(kPresetNameProperty, juce::String());

        // The program is found by name first, since adding a factory preset
        // shifts the indices of the ones after it. A name that is not a factory
        // preset keeps the saved index.
        const auto known = ovt::presets::names().indexOf(loadedPresetName);

        currentProgram =
            known >= 0 ? known
            : juce::isPositiveAndBelow(saved, ovt::presets::names().size())
                ? saved
                : 0;

        // Whatever was restored, a host program change to the same index must
        // not load a preset over it.
        programApplied = true;

        apvts.replaceState(tree);
      }

      // After replaceState, so bindings resolve against this session's
      // parameters.
      midiLearn.fromTree(tree.getChildWithName(ovt::MidiLearn::kTreeType),
                         *this);

      // A macro with no saved channels reaches none.
      for (int m = 0; m < ovt::params::kNumMacros; ++m)
        paramCache.macroChannels[(size_t)m].store(ovt::params::channelListMask(
            tree.getProperty(macroChannelsProperty(m)).toString()));

      // A value that is not a number, from a corrupted or hand edited file, is
      // put back to the default. NaN is the one value the range clamps let
      // through, and checking it here keeps the check off the audio thread.
      for (auto *parameter : getParameters())
        if (!std::isfinite(parameter->getValue()))
          parameter->setValueNotifyingHost(parameter->getDefaultValue());
    }
  }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new OvertoniumProcessor();
}
