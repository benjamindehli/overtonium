#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginParameters.h"
#include "../dsp/Lfo.h"

namespace ovt::ui {

/// Picks the shape a modulator traces, and draws the one it is set to.
///
/// A glyph rather than a word. A strip is 38 px across, which is three
/// characters of anything readable and not enough for "Reverse Sawtooth", and
/// a drawing says what a sawtooth is faster than the word does to anyone who
/// has seen one. The names are still there, in the menu the glyph opens, which
/// is where you go when you want to be sure.
///
/// One turn of the wave is drawn, left to right, so the picture is the same
/// thing the modulator will do rather than a symbol standing for it.
class ShapeButton : public juce::Component {
public:
  /// @param shapes  the list this destination offers, which differs between
  ///                pitch and amplitude. Indexes match the parameter's.
  /// @param suffix  the same control on every other channel, for the entry
  ///                 that sets them all. LINK cannot do this one: it drags a
  ///                 value across the series by a weighted curve and there is
  ///                 no weighting a list of named shapes.
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

  LfoShape selectedShape() const;

  /// Reported to a screen reader, since a glyph has no text of its own.
  juce::String currentName() const;

  /// Whether this modulator is one circuit the whole keyboard hears.
  ///
  /// A global switch shown in a per-channel menu, which is where it belongs:
  /// the menu is the modulator's own, and every channel's answer is the same
  /// one. See GlobalParams::ampModInPhase.
  bool inPhase() const;

  /// The menu the glyph opens, as data.
  ///
  /// Built here rather than inside the click so a test can walk it. Showing a
  /// menu needs a real window, which is exactly what the headless build has
  /// not got, and the entries are the part worth checking anyway.
  juce::PopupMenu buildMenu() const;

private:
  void applyToEveryChannel(int index);

  juce::AudioProcessorValueTreeState &apvts;
  void setInPhase(bool);

  /// Which of the two switches this button's modulator answers to, picked off
  /// the suffix it already carries for setting every channel at once.
  const char *inPhaseId() const;

  juce::String id;
  const char *sharedSuffix;
  juce::Span<const LfoShape> offered;
  juce::StringArray names;
  bool hovered = false;

  /// Repaints when the parameter moves, which is mostly not from here.
  ///
  /// A preset load, an undo or a host's automation all change the shape
  /// without anyone touching this button, and the glyph is drawn from the
  /// parameter at paint time, so without a listener it kept showing the last
  /// shape chosen by hand while the sound had already moved on. The attachment
  /// carries the other direction too, so JUCE owns the gesture and the undo
  /// transaction rather than this doing it by hand.
  ///
  /// Last, and that matters. Members are destroyed in reverse declaration
  /// order, so anything below this would still be listening to the parameter
  /// after the fields it draws from had gone. A parameter moved on the message
  /// thread calls the listener straight through rather than queueing it, so
  /// that window is reachable rather than theoretical. Declared last, it stops
  /// listening first.
  std::unique_ptr<juce::ParameterAttachment> attachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShapeButton)
};

} // namespace ovt::ui
