#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginParameters.h"
#include "../dsp/Lfo.h"
#include "PaintCache.h"

namespace ovt::ui {

/// Picks the shape a modulator traces, and draws one turn of the shape it is
/// set to. A click opens a menu of the shapes by name.
class ShapeButton : public juce::Component {
public:
  /// @param shapes  the list this destination offers, which differs between
  ///                pitch and amplitude. Indexes match the parameter's.
  /// @param suffix  the same control on every other channel, for the entry
  ///                 that sets them all
  ShapeButton(juce::AudioProcessorValueTreeState &state,
              const juce::String &parameterId, const char *suffix,
              juce::Span<const LfoShape> shapes, juce::StringArray shapeNames);

  void paint(juce::Graphics &) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseEnter(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

  /// Draws one turn of a shape inside `area`. Free of any parameter, so the
  /// menu and the docs renderer can use it too.
  static void drawShape(juce::Graphics &, juce::Rectangle<float> area, LfoShape,
                        juce::Colour);

  /// Which entry of the list is selected, or 0 if the parameter has gone.
  int selectedIndex() const;

  /// The shape that is selected.
  LfoShape selectedShape() const;

  /// Reported to a screen reader, since a glyph has no text of its own.
  juce::String currentName() const;

  /// Whether this modulator is one circuit the whole keyboard hears. A global
  /// switch, shown in each channel's menu. See GlobalParams::ampModInPhase.
  bool inPhase() const;

  /// The menu the glyph opens, as data, so tests can walk it without a window.
  juce::PopupMenu buildMenu() const;

private:
  /// Sets this shape on every channel that has this control.
  void applyToEveryChannel(int index);

  /// Everything paint draws, before it is kept as a picture.
  void paintShape(juce::Graphics &);

  juce::AudioProcessorValueTreeState &apvts;

  /// Writes the shared switch.
  void setInPhase(bool);

  /// Which of the two shared switches this button's modulator answers to,
  /// picked by its suffix.
  const char *inPhaseId() const;

  juce::String id;
  const char *sharedSuffix;
  juce::Span<const LfoShape> offered;
  juce::StringArray names;
  bool hovered = false;

  /// Its last two looks, kept as pictures.
  PaintCache looks;

  /// Repaints when the parameter moves from elsewhere, and writes it with a
  /// gesture when a shape is chosen here.
  ///
  /// Declared last so it is destroyed first and stops listening before the
  /// fields it draws from are gone.
  std::unique_ptr<juce::ParameterAttachment> attachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShapeButton)
};

} // namespace ovt::ui
