#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../MidiLearn.h"

namespace ovt::ui {

/// The MIDI Learn end of a right-click, kept in one place because the menu it
/// joins is built somewhere else for every control that has one.
///
/// A control carries the id of the parameter it moves, set where the
/// attachment is made, since that is the one place that already knows it.
/// JUCE's component properties are how this codebase hangs facts on controls
/// already, so there is nothing new to maintain: see the "meteredGroove" flag
/// a fader sets for the look and feel.
namespace learn {

inline const juce::Identifier kParameterProperty{"paramId"};

/// Item ids, well clear of the menus these join.
enum MenuIds { kLearn = 9001, kCancel = 9002, kForget = 9003 };

inline void tag(juce::Component &control, const juce::String &parameterId) {
  control.getProperties().set(kParameterProperty, parameterId);
}

/// The parameter a click landed on, looking outwards from the component that
/// was hit.
///
/// Outwards because a control is rarely one component: a knob is a slider
/// inside a labelled frame, and a click on the number under it lands on a
/// label whose parent is the thing with the id. Stops at the first one found,
/// so a tagged control inside a tagged container answers for itself.
inline juce::String parameterIdAt(const juce::Component *hit) {
  for (const auto *c = hit; c != nullptr; c = c->getParentComponent()) {
    const auto id = c->getProperties().getWithDefault(kParameterProperty, {});

    if (!id.toString().isEmpty())
      return id.toString();
  }

  return {};
}

/// Adds the learn items to a menu that is about to be shown, if the click
/// landed on something a controller could move.
///
/// @return true if anything was added, so the caller knows whether it needs a
///         separator above them.
inline bool appendItems(juce::PopupMenu &menu, const MidiLearn &map,
                        juce::RangedAudioParameter *parameter) {
  if (parameter == nullptr)
    return false;

  menu.addSeparator();

  const auto waiting = map.armed() == parameter;
  const auto cc = map.controllerFor(parameter);

  if (waiting) {
    // Named rather than ticked, because what it is waiting for is the thing
    // somebody has to do next and a tick says nothing about that.
    menu.addSectionHeader("Move a controller to bind it");
    menu.addItem(kCancel, "Stop waiting");

    return true;
  }

  if (cc >= 0) {
    menu.addSectionHeader("Bound to CC " + juce::String(cc));
    menu.addItem(kForget, "Forget CC " + juce::String(cc));
    menu.addItem(kLearn, "Bind a different controller");

    return true;
  }

  menu.addItem(kLearn, "MIDI Learn");

  return true;
}

/// Acts on one, if it was one of these.
///
/// @return true when the id belonged here, so the caller stops looking.
inline bool applyChoice(int result, MidiLearn &map,
                        juce::RangedAudioParameter *parameter) {
  if (parameter == nullptr)
    return false;

  switch (result) {
  case kLearn:
    map.arm(parameter);
    return true;

  case kCancel:
    map.arm(nullptr);
    return true;

  case kForget:
    map.forget(parameter);
    return true;

  default:
    return false;
  }
}

} // namespace learn
} // namespace ovt::ui
