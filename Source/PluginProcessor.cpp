#include "PluginProcessor.h"
#include "Presets.h"

#include <utility>

namespace {
/// One step in the history: everything a person moved in one go.
///
/// Values rather than a description, because what a step has to be able to do
/// is put things back exactly, and normalised because that is what a
/// parameter takes and gives without a range having to agree with anything.
///
/// Only what actually moved is kept. A preset load writes every parameter and
/// changes perhaps a third of them, and a step that carried all 782 would be
/// mostly a record of values that were already what they are.
class ParameterEdit final : public juce::UndoableAction {
public:
  struct Change {
    juce::RangedAudioParameter *param;
    float before, after;
  };

  ParameterEdit(std::vector<Change> movedIn, juce::String nameIn)
      : moved(std::move(movedIn)), name(std::move(nameIn)) {}

  /// Nothing, the first time. A step is pushed once the change it describes
  /// has already happened, so writing the values again would only tell every
  /// host in the room about a move it has just been told about. A redo is a
  /// real performance and does write them.
  bool perform() override {
    if (std::exchange(pushed, true))
      return apply(&Change::after);

    return true;
  }

  bool undo() override { return apply(&Change::before); }

  int getSizeInUnits() override { return (int)(moved.size() * sizeof(Change)); }

private:
  bool apply(float Change::*which) {
    for (const auto &change : moved)
      if (change.param != nullptr)
        change.param->setValueNotifyingHost(change.*which);

    return true;
  }

  std::vector<Change> moved;
  juce::String name;
  bool pushed = false;
};
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

  // Its own listener, which is how a person moving a control is told apart
  // from a host writing a lane: a control opens a gesture around what it
  // writes and automation does not. See
  // audioProcessorParameterChangeGestureBegin.
  addListener(this);

  // Slow, because the only thing it carries is a preset asked for over MIDI.
  // Fifty milliseconds is shorter than a thirty-second note at 120 bpm, so a
  // program change sent ahead of the phrase it is for arrives in time, and a
  // tick this cheap can be left running with no window open, which is where a
  // program change still has to work.
  startTimerHz(20);
}

OvertoniumProcessor::~OvertoniumProcessor() {
  // Before anything else goes. Timer is a base class, so it is not destroyed
  // until last, and a tick that arrives while the members above it are being
  // torn down calls a virtual on an object that is half gone. The same goes
  // for the listener.
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

    // Exactly, rather than within a tolerance. These are stored values and
    // not computed ones, so a parameter that was moved and moved back holds
    // the bits it started with: a parameter that came back to where it
    // started during a gesture has not been edited, and one that moved by a
    // hair has. Written as two comparisons because a compiler told to distrust
    // == between floats cannot tell this case from an arithmetic one.
    if (!(before < after) && !(after < before))
      continue;

    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(params[i]))
      moved.push_back({ranged, before, after});
  }

  editBaseline.clear();

  if (moved.empty())
    return;

  // Its own transaction, so one gesture is one step whatever else has
  // happened since.
  undoManager.beginNewTransaction(name);
  undoManager.perform(new ParameterEdit(std::move(moved), name));
}

void OvertoniumProcessor::recordEdit(const juce::String &name,
                                     const std::function<void()> &change) {
  if (change == nullptr)
    return;

  // Nested recordings would each take a baseline and the inner one would win,
  // so the outer step would hold only part of what it did.
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
  // Only what arrives on the message thread, which is where a person moving a
  // control is. A host is allowed to open a gesture from the audio thread,
  // and taking a baseline of 782 parameters there would allocate on it.
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

  // A floor under whatever the host asks for, so a host that promises a very
  // small block and then hands over a large one is not cut into a great many
  // pieces. Two channels of 512 frames is four kilobytes.
  scratch.setSize(2, juce::jmax(512, maximumExpectedSamplesPerBlock), false,
                  true, true);
  pitchBendNormalised = 0.0f;
  channelPressure = 0.0f;
  modWheel = 0.0f;
}

bool OvertoniumProcessor::mpeIsOn() const noexcept {
  return paramCache.mpe != nullptr && paramCache.mpe->load() > 0.5f;
}

void OvertoniumProcessor::setMpeEnabled(bool on) {
  mpeWasOn = on;

  // Whatever is sounding was started by the other set of entry points and can
  // no longer be reached by the one about to take over, so it would hang.
  mpeInstrument.releaseAllNotes();
  engine.allNotesOff();

  if (!on) {
    mpeInstrument.setZoneLayout({});
    return;
  }

  // A lower zone with every remaining channel as a member, which is what a
  // controller sends unless it says otherwise, and it is free to say
  // otherwise: the layout messages it sends are parsed and will replace this.
  //
  // The master range is taken from the panel so that the wheel spans what the
  // BEND knob says it does, the same as it would with MPE off. The per-note
  // range is left at the 48 semitones the specification asks for, since that
  // one belongs to the controller rather than to the panel.
  const auto masterRange =
      paramCache.bendRange != nullptr
          ? juce::jlimit(1, 96, (int)std::lround(paramCache.bendRange->load()))
          : 2;

  juce::MPEZoneLayout layout;
  layout.setLowerZone(15, 48, masterRange);
  mpeInstrument.setZoneLayout(layout);

  // The wheel's own contribution is folded into each note's bend from here on,
  // and pressure arrives per note, so leaving either value behind would apply
  // it a second time across everything.
  pitchBendNormalised = 0.0f;
  channelPressure = 0.0f;
}

// -----------------------------------------------------------------------------
// The notes an MPE controller sends. Each owns a channel, and its bend and
// pressure arrive on that channel rather than across the instrument.

void OvertoniumProcessor::noteAdded(juce::MPENote note) {
  engine.noteOnPerNote(note.midiChannel, note.initialNote,
                       note.noteOnVelocity.asUnsignedFloat(), currentParams);

  // A note can arrive already bent and already pressed, because the controller
  // sets the channel up before it sends the note on. Applying both here is
  // what stops a finger that lands on the way into a bend from starting flat
  // and jumping.
  notePitchbendChanged(note);
  notePressureChanged(note);

  // The slide deliberately not, where the other two are. A new note is handed
  // the centre of the timbre axis whenever its channel has heard no CC74, and
  // that is a value JUCE supplied rather than one the controller sent. Feeding
  // it in here would make the centre this note's rest position, and then a
  // controller whose slide actually rests at one end of the axis lurches the
  // moment it sends its first real value. The voice takes its nought from that
  // first value instead. See Voice::setSlide.
}

void OvertoniumProcessor::notePressureChanged(juce::MPENote note) {
  engine.setNotePressure(note.midiChannel, note.initialNote,
                         note.pressure.asUnsignedFloat());
}

void OvertoniumProcessor::noteTimbreChanged(juce::MPENote note) {
  // Bipolar around the rest position. JUCE starts a note's timbre at the
  // centre value, so a controller that sends no CC74 at all lands on zero here
  // and changes nothing, which is what it should do.
  engine.setNoteSlide(note.midiChannel, note.initialNote,
                      note.timbre.asUnsignedFloat() * 2.0f - 1.0f);
}

void OvertoniumProcessor::notePitchbendChanged(juce::MPENote note) {
  // Already the sum of the note's own bend and the master channel's, each
  // against its own range.
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

  case AftertouchSource::Either:
    currentParams.global.aftertouch = juce::jmax(channelPressure, modWheel);
    break;
  }
}

void OvertoniumProcessor::handleMidiMessage(const juce::MidiMessage &m) {
  if (mpeWasOn) {
    // Notes, bend, pressure, the pedal and the layout messages are all the
    // parser's, on every channel it has been given. That includes the master
    // channel, so an ordinary keyboard on channel 1 still plays: its notes
    // become notes of the master channel, one voice per key, moved together by
    // the wheel exactly as they would be with this switched off.
    mpeInstrument.processNextMidiEvent(m);

    // What it does not touch. The wheel is CC 1 rather than the pitch wheel,
    // and all-sound-off is CC 120, which it leaves alone: only CC 123, let go
    // of everything, is its business. So that one has to reach the pool
    // directly, since it means stop now rather than let go.
    if (m.isAllSoundOff())
      mpeInstrument.releaseAllNotes(); // or it goes on holding notes that are
                                       // already gone from the pool

    // A program change is nothing to do with playing a note either, and an MPE
    // controller sends it on a member channel like everything else it sends.
    const bool leftOver = (m.isController() && m.getControllerNumber() == 1) ||
                          m.isAllSoundOff() || m.isProgramChange();

    if (!leftOver)
      return;
  }

  handleOrdinaryMidiMessage(m);
}

void OvertoniumProcessor::handleOrdinaryMidiMessage(
    const juce::MidiMessage &m) {
  if (m.isNoteOn()) {
    engine.noteOn(m.getNoteNumber(), m.getFloatVelocity(), currentParams);
  } else if (m.isNoteOff()) {
    // getVelocity is the release velocity on a note-off, and zero on the
    // note-on-of-velocity-zero form that most keyboards send instead of
    // one. liftFromVelocity reads zero as neutral for that reason.
    engine.noteOff(m.getNoteNumber(), ovt::liftFromVelocity(m.getVelocity()));
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
  } else if (m.isProgramChange()) {
    // Written down rather than acted on. See pendingProgram for why the audio
    // thread cannot be the one to load it.
    //
    // Nor range checked. A program change can name any of 128 programs and
    // there are fewer presets than that, so most of what it can say names
    // nothing, and deciding that belongs where the list is read.
    pendingProgram.store(m.getProgramChangeNumber(), std::memory_order_relaxed);
  }
}

void OvertoniumProcessor::timerCallback() { applyPendingProgramChange(); }

void OvertoniumProcessor::applyPendingProgramChange() {
  const int index = pendingProgram.exchange(-1, std::memory_order_relaxed);

  if (index < 0)
    return;

  // Through the same door the plugin's own menu uses, not through
  // setCurrentProgram, and for the same reason: a program change is somebody
  // asking for that preset, so it loads even when it is the one already
  // showing, and what you had changed is replaced. That is what a program
  // change does on an instrument with a panel, and it is what makes a clip
  // that begins with one sound the same on every pass.
  //
  // Going this way also tells the host which program is loaded now, so an
  // Audio Unit host's own preset menu follows a program change rather than
  // going stale against it.
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
  updateAftertouch();
  engine.setPolyphony(paramCache.polyphonyValue());
  engine.setLegato(paramCache.legatoValue());

  // Hosts are allowed to hand over a bigger block than the one they promised
  // in prepareToPlay. Growing the scratch here would be an allocation on the
  // audio thread, which takes the allocator's lock and can be made to wait by
  // whatever the message thread happens to be doing, so the block is cut into
  // pieces the scratch already holds instead. The common case is one piece.
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

      // Anything past this piece waits for the next one. On the last piece
      // there is no next one, so the rest is taken here rather than dropped.
      if (at >= chunkEnd && !lastChunk)
        break;

      // Clamping against `position` rather than the start keeps the segments
      // monotonic even if a host hands us out-of-order timestamps; otherwise
      // we would render backwards over audio we had already written.
      const auto eventTime = juce::jlimit(position, chunkEnd, at);

      renderSegment(position - done, eventTime - position);
      position = eventTime;

      handleMidiMessage(metadata.getMessage());
      ++event;
    }

    renderSegment(position - done, chunkEnd - position);

    // ---- write the mix out --------------------------------------------------
    //
    // Channel indices worked out here and bounds-checked, rather than through
    // getBusBuffer. A host is supposed to hand over a buffer with a channel
    // for every enabled bus and not all of them do, and getBusBuffer points
    // confidently past the end of one that is short.
    //
    // There is a single output bus and isBusesLayoutSupported takes only mono
    // or stereo, so the widest thing written here is two channels and the bus
    // never has a spare one to clear. The count is still read rather than
    // assumed, since it is the host that decides which of the two it asked
    // for.
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
}

int OvertoniumProcessor::getNumPrograms() {
  // The Audio Unit only, and that is the whole reason this exists: Logic and
  // GarageBand read factory presets through the program interface and through
  // nothing else, so without it their preset menus list one entry.
  //
  // Every other format gets one. A program count above one makes JUCE's VST3
  // wrapper publish an automatable "Program" parameter, and moving that
  // parameter rewrites all eight hundred of the others. That is a
  // poor thing to hand an automation lane, it changes the parameter set of a
  // plugin already released without it, and pluginval's state restoration
  // test fails against it. VST3 hosts have preset handling of their own and
  // lose nothing, since the plugin's own preset menu reaches every factory
  // preset on every format regardless.
  return wrapperType == wrapperType_AudioUnit ? ovt::presets::names().size()
                                              : 1;
}

const juce::String OvertoniumProcessor::getProgramName(int index) {
  const auto all = ovt::presets::names();

  // Hosts ask about indices they have cached, and a cache outlives the list it
  // was taken from. An empty string is a name a host can show.
  return juce::isPositiveAndBelow(index, all.size()) ? all[index]
                                                     : juce::String();
}

void OvertoniumProcessor::setCurrentProgram(int index) {
  // Hosts call this on their own account, not only when someone picks from a
  // menu. Restoring a VST3 session sets every parameter, and JUCE's wrapper
  // makes the program one of them, so a session saved on preset 3 arrives here
  // asking for preset 3 with the user's edits to it already restored. Loading
  // it again would throw those edits away.
  //
  // Doing nothing when the index has not changed is what makes that safe, and
  // it is why currentProgram travels in the saved state.
  //
  // Unless nothing has been loaded yet. A new instance reports program zero
  // because some number has to be reported, while the sound is the parameter
  // defaults rather than that preset, so the first entry in a host's menu
  // looks selected and picking it did nothing at all. The flag is what tells
  // the two apart: an index that matches because a preset was loaded, and one
  // that matches because none has been.
  if (index == currentProgram && programApplied)
    return;

  applyFactoryPreset(index);
}

void OvertoniumProcessor::applyFactoryPreset(int index) {
  // Against the number of presets, not getNumPrograms. Those differ on every
  // format except the Audio Unit, and this is the path the plugin's own menu
  // takes, which reaches every one of them everywhere.
  if (!juce::isPositiveAndBelow(index, ovt::presets::names().size()))
    return;

  currentProgram = index;
  programApplied = true;
  loadedPresetName = ovt::presets::names()[index];
  ovt::presets::apply(apvts, index);

  // So a host showing the preset name updates when the change came from the
  // plugin's own menu rather than from the host's.
  updateHostDisplay(
      juce::AudioProcessorListener::ChangeDetails{}.withProgramChanged(true));
}

void OvertoniumProcessor::setLoadedPresetName(const juce::String &name) {
  loadedPresetName = name;

  // A preset of your own is not one of the factory programs, so the host's
  // program index no longer describes what is loaded. Saying so stops a
  // redundant program change from the host reaching in and replacing it.
  programApplied = true;
}

/// The property the current program travels under. Namespaced enough not to
/// collide with a parameter id, since both live in the same tree.
static const juce::Identifier kCurrentProgramProperty{"overtoniumProgram"};

/// What the preset button says, which the editor cannot work out on its own.
///
/// The program index only names a factory preset, and a preset of your own is
/// not one of those, so the name travels separately. Without it a window
/// reopened on a session showed nothing loaded whatever was.
static const juce::Identifier kPresetNameProperty{"overtoniumPresetName"};

void OvertoniumProcessor::getStateInformation(juce::MemoryBlock &destData) {
  auto state = apvts.copyState();
  state.setProperty(kCurrentProgramProperty, currentProgram, nullptr);
  state.setProperty(kPresetNameProperty, loadedPresetName, nullptr);

  if (auto xml = state.createXml())
    copyXmlToBinary(*xml, destData);
}

void OvertoniumProcessor::setStateInformation(const void *data,
                                              int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
    if (xml->hasTagName(apvts.state.getType())) {
      const auto tree = juce::ValueTree::fromXml(*xml);

      // Read before replaceState, which is what makes the host's own program
      // restore a no-op rather than a reload. A state written before this
      // property existed has no such value and lands on the first preset,
      // which is the same answer the old build gave.
      const int saved = tree.getProperty(kCurrentProgramProperty, 0);

      loadedPresetName = tree.getProperty(kPresetNameProperty, juce::String());

      // The name decides which program this is, and the index is only the
      // fallback. Factory presets sort alphabetically, so adding one moves
      // every preset after it, and a session written before that move carries
      // an index that now names a different sound. The sound itself is never
      // in doubt, since the state holds the parameter values and nothing here
      // reloads a preset over them, but the index is what a host shows as
      // selected in its own menu, and it would be pointing at a patch the
      // session is not playing.
      //
      // A name that is not a factory preset is one of the user's own, or a
      // patch edited since it was loaded, and neither has a program to be.
      // Those keep the saved index, which is the answer this always gave.
      const auto known = ovt::presets::names().indexOf(loadedPresetName);

      currentProgram =
          known >= 0 ? known
          : juce::isPositiveAndBelow(saved, ovt::presets::names().size())
              ? saved
              : 0;

      // Whatever the state said, it is a state, so a program change from the
      // host asking for the index just restored has nothing left to do. Set
      // even when the properties were absent, since a session written before
      // they existed still restored a sound that must not be overwritten by
      // the preset its index happens to name.
      programApplied = true;

      apvts.replaceState(tree);
    }
  }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new OvertoniumProcessor();
}
