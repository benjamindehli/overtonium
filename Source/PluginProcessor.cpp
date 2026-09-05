#include "PluginProcessor.h"
#include "Presets.h"

juce::AudioProcessor::BusesProperties OvertoniumProcessor::buses() {
  return BusesProperties().withOutput("Output",
                                      juce::AudioChannelSet::stereo(), true);
}

OvertoniumProcessor::OvertoniumProcessor()
    : juce::AudioProcessor(buses()),
      apvts(*this, &undoManager, "OVERTONIUM",
            ovt::params::createParameterLayout()) {
  paramCache.connect(apvts);
  mpeInstrument.addListener(this);
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

  // And the slide, for the same reason: a finger that lands forward on the pad
  // has already set the timbre before the note arrives.
  noteTimbreChanged(note);
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
  engine.noteOffPerNote(note.midiChannel, note.initialNote,
                        note.noteOffVelocity.asUnsignedFloat());
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

    const bool leftOver =
        (m.isController() && m.getControllerNumber() == 1) || m.isAllSoundOff();

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
    // A controller that ends a note with velocity zero, whether as a real
    // note-off or as the note-on-with-zero many of them send, has said nothing
    // about how the key came up. That gets the middle value rather than the
    // slowest, which would otherwise silence every key-off on such a keyboard.
    const auto lift =
        m.getVelocity() == 0 ? 0.5f : m.getFloatVelocity();

    engine.noteOff(m.getNoteNumber(), lift);
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
  // parameter rewrites all six hundred and seventy of the others. That is a
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
      currentProgram =
          juce::isPositiveAndBelow(saved, ovt::presets::names().size()) ? saved
                                                                       : 0;

      loadedPresetName = tree.getProperty(kPresetNameProperty, juce::String());

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
