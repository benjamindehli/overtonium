#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginParameters.h"

namespace ovt::ui {

/// The two glide switches, at the end of the menu a right-click on any
/// channel's GLIDE knob opens.
///
/// They govern every channel at once and belong to the patch, which is the
/// position the modulators' "in phase" switches are in, and those live in the
/// menu of the control they are about for the same reason these do: the knob
/// is where somebody setting up a glide already is. Kept in one place, like
/// the learn items, because the menu they join is built somewhere else.
namespace glide {

/// Item ids, clear of the LINK menu and of the learn items at 9001.
enum MenuIds { kAlways = 9101, kLegato = 9102, kRate = 9103, kTime = 9104 };

/// Whether a parameter is one of the channels' glide times.
inline bool isGlideTime(const juce::String &parameterId) {
  return parameterId.endsWith(juce::String("_") + params::glideSuffix);
}

/// Adds the switches to a menu that is about to be shown, if the click landed
/// on a glide time.
///
/// @return true if anything was added.
inline bool appendItems(juce::PopupMenu &menu,
                        juce::AudioProcessorValueTreeState &apvts,
                        const juce::String &parameterId) {
  auto *trigger = apvts.getParameter(params::glideTriggerId);
  auto *mode = apvts.getParameter(params::glideModeId);

  if (!isGlideTime(parameterId) || trigger == nullptr || mode == nullptr)
    return false;

  const bool legato = trigger->getValue() > 0.5f;
  const bool time = mode->getValue() > 0.5f;

  menu.addSeparator();
  menu.addSectionHeader("Glide (every channel)");
  menu.addItem(kAlways, "Always", true, !legato);
  menu.addItem(kLegato, "Legato", true, legato);
  menu.addSeparator();
  menu.addItem(kRate, "Fixed rate", true, !time);
  menu.addItem(kTime, "Fixed time", true, time);

  return true;
}

/// Acts on one, if it was one of these. A gesture each, so a host records the
/// change as one step and an undo takes it back as one.
///
/// @return true when the id belonged here, so the caller stops looking.
inline bool applyChoice(int result, juce::AudioProcessorValueTreeState &apvts) {
  const auto set = [&apvts](const char *id, float value) {
    if (auto *param = apvts.getParameter(id)) {
      param->beginChangeGesture();
      param->setValueNotifyingHost(value);
      param->endChangeGesture();
    }
  };

  switch (result) {
  case kAlways:
  case kLegato:
    set(params::glideTriggerId, result == kLegato ? 1.0f : 0.0f);
    return true;

  case kRate:
  case kTime:
    set(params::glideModeId, result == kTime ? 1.0f : 0.0f);
    return true;

  default:
    return false;
  }
}

} // namespace glide
} // namespace ovt::ui
