#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../MidiLearn.h"

namespace ovt::ui {

/// The MIDI Learn items of a right-click menu, shared by every menu that
/// offers them.
///
/// A control carries the id of the parameter it moves as a component
/// property, set where its attachment is made.
namespace learn {

inline const juce::Identifier kParameterProperty{"paramId"};

/// Item ids, well clear of the menus these join.
enum MenuIds { kLearn = 9001, kCancel = 9002, kForget = 9003 };

/// Marks a control with the parameter it moves.
inline void tag(juce::Component &control, const juce::String &parameterId) {
  control.getProperties().set(kParameterProperty, parameterId);
}

/// The parameter a click landed on, looking outwards from the component that
/// was hit, since a click may land on a part inside the tagged control. The
/// innermost tag wins.
inline juce::String parameterIdAt(const juce::Component *hit) {
  for (const auto *c = hit; c != nullptr; c = c->getParentComponent()) {
    const auto id = c->getProperties().getWithDefault(kParameterProperty, {});

    if (!id.toString().isEmpty())
      return id.toString();
  }

  return {};
}

/// The control that moves this parameter, looking inwards from a window.
inline juce::Component *controlFor(juce::Component &root,
                                   const juce::String &parameterId) {
  if (parameterId.isEmpty())
    return nullptr;

  for (auto *child : root.getChildren()) {
    const auto id =
        child->getProperties().getWithDefault(kParameterProperty, {});

    if (id.toString() == parameterId)
      return child;

    if (auto *found = controlFor(*child, parameterId))
      return found;
  }

  return nullptr;
}

/// Hangs the waiting marker on a control, or takes it off. The look and feel
/// draws it: see the "learnArmed" property in OvertoniumLookAndFeel.
inline void markArmed(juce::Component &control, bool armed) {
  if (armed)
    control.getProperties().set("learnArmed", true);
  else
    control.getProperties().remove("learnArmed");

  // And on the slider or button inside it, which is what the look and feel
  // draws, since the tag may sit on an outer frame.
  for (auto *child : control.getChildren())
    if (dynamic_cast<juce::Slider *>(child) != nullptr ||
        dynamic_cast<juce::Button *>(child) != nullptr)
      markArmed(*child, armed);

  control.repaint();
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
    // A header saying what to do next, rather than a tick.
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
