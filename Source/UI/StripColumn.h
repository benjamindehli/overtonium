#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ovt::ui {

/// What a channel strip and the noise strip have in common: the rows they lay
/// out, how far those are scrolled and folded, the row and the column the
/// pointer lights, the fader a drawn line sets, and the lights a solo
/// elsewhere turns off.
class StripColumn : public juce::Component, public juce::SettableTooltipClient {
public:
  /// Which groups of rows are folded away. Set by the editor on every column
  /// at once, since the rows are shared across the whole mixer.
  void setCollapsedSections(SectionMask);

  /// How far the parameters are scrolled, shared by every column.
  ///
  /// The gutter and every strip get the same number, so captions line up with
  /// their knobs. Snapped to a row boundary inside layoutRows.
  void setScroll(int);

  /// Asked for when a click lands on one of the rules between sections, which
  /// line up with the gutter's headings and do the same thing.
  std::function<void(Section)> onSectionToggled;

  /// Sets this column's fader from a height, for a drag that is drawing
  /// across the mixer rather than moving one fader.
  ///
  /// @p y is in this column's own coordinates. Read through the slider, whose
  /// range is skewed to match the parameter's.
  void drawFaderAt(int y);

  /// The row the pointer is on, with the rules between sections counted as
  /// rows of their own. See reportHover.
  static Row rowUnder(const RowBounds &, juce::Point<int>);

  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;

  /// The heading wash and the column highlight. Over the children, since the
  /// activity lamps on the heading rows paint opaque backdrops.
  void paintOverChildren(juce::Graphics &) override;

  /// Turns the column's lights and screens off when another channel's solo is
  /// silencing it. Its mute and solo stay lit.
  void setSilencedByOthers(bool shouldDim);

  /// Whether the pointer is on this column, which is what lights it.
  bool isHovered() const noexcept { return hovered; }

  /// Picks out one row, or kNoRow to clear. Every column is told the same
  /// row, so the highlight runs the width of the mixer.
  void setHighlightedRow(Row);

protected:
  explicit StripColumn(HoverTarget &hoverTarget) : hover(hoverTarget) {}

  /// Where every row sits at the current fold and scroll.
  RowBounds currentRows() const;

  /// Tells the editor which row the pointer is on. Called for the column's
  /// own mouse events and for those of every control on it.
  void reportHover(const juce::MouseEvent &);

  /// Lets go of the hover without a mouse event to say so, for when a modal
  /// menu is about to take the pointer away.
  void clearHover();

  /// The colour of the column's background at a height, for a meter or a
  /// lamp that paints an opaque backdrop.
  juce::Colour backdropAt(int y) const;

  /// The level a fader's gain reads as, -inF at the bottom.
  static juce::String levelReading(float gain);

  /// The partial this column is, as the editor numbers them, or -1 for the
  /// noise channel, which sits outside the series.
  virtual int hoverIndex() const = 0;

  /// The fader a drawn line sets.
  virtual juce::Slider &fader() = 0;

  /// A row that holds this column's own controls rather than what the gutter
  /// names, which takes no part in the row highlight.
  virtual bool isOwnRow(Row) const { return false; }

  /// Called once the lights have been turned off or on again, for anything
  /// else that shows the change.
  virtual void silenceChanged() {}

  HoverTarget &hover;

  bool silenced = false;
  Row highlighted = kNoRow;

  /// Folded groups, set by the editor. Every layout and hit test follows it.
  SectionMask collapsed = 0;
  int scroll = 0;

  bool hovered = false;

  /// Set when a menu takes the pointer away, and cleared when the pointer
  /// moves under its own steam again. See clearHover.
  bool hoverSuppressed = false;
};

} // namespace ovt::ui
