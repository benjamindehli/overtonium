#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/// The lists both the Settings menu and the option bars offer, built and
/// carried out in one place so the two agree.
namespace ovt::ui::settings {

/// Item ids, the same wherever the item appears. Each list counts up from its
/// base, one id per choice, in the order the choices are listed.
enum Id : int {
  kVoices = 100,
  kBendRange = 200,
  kPhaseReset = 300,
  kSafetyClip = 301,
  kMpe = 302,
  kOneVoicePerKey = 303,
  kLookahead = 304,
  kLegato = 305,
  kHarmonicSevenths = 306,
  kAdaptiveHome = 307,
  kAftertouchFrom = 600,
  kTemperament = 900,
  kRoot = 1000,
  kReferencePitch = 1100,
  kSlideTo = 1200,
  kRetune = 1300,
};

/// The voice counts, from one up, with the one in use ticked.
juce::PopupMenu voices(juce::AudioProcessorValueTreeState &);

/// The common bend ranges, in semitones.
juce::PopupMenu bendRange(juce::AudioProcessorValueTreeState &);

/// Which control the AT row listens to: pressure, the wheel or either.
juce::PopupMenu aftertouchFrom(juce::AudioProcessorValueTreeState &);

/// What an MPE slide moves.
juce::PopupMenu slideTo(juce::AudioProcessorValueTreeState &);

/// The temperaments, the root they are built on, and the reference pitch.
juce::PopupMenu temperament(juce::AudioProcessorValueTreeState &);
juce::PopupMenu root(juce::AudioProcessorValueTreeState &);
juce::PopupMenu referencePitch(juce::AudioProcessorValueTreeState &);

/// Fast and Smooth, two quick settings of how long Adaptive takes to move a
/// held note.
juce::PopupMenu retune(juce::AudioProcessorValueTreeState &);

/// How many voices are in use, counting the voice list's old legato entry as
/// the one voice it was.
int voiceCount(juce::AudioProcessorValueTreeState &);

/// Whether Legato is on, counting the old legato entry, which was.
bool legatoOn(juce::AudioProcessorValueTreeState &);

/// Whether a switch parameter is on.
bool isOn(juce::AudioProcessorValueTreeState &, const char *id);

/// The index of a choice parameter's current choice.
int choiceIndex(juce::AudioProcessorValueTreeState &, const char *id);

/// Whether the root decides anything you can hear: under any temperament but
/// equal, or with the resonator in Series.
bool rootInUse(juce::AudioProcessorValueTreeState &);

/// Carries out a choice from any of the lists above, or one of the switches.
/// Returns false, and does nothing, for an id that is none of them.
bool apply(juce::AudioProcessorValueTreeState &, int id);

} // namespace ovt::ui::settings
