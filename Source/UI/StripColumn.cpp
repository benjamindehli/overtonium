#include "StripColumn.h"

#include "../PluginParameters.h"
#include "LookAndFeel.h"
#include "MuteSoloButton.h"
#include "ShapeButton.h"

namespace ovt::ui {

RowBounds StripColumn::currentRows() const {
  return layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed,
                    scroll);
}

void StripColumn::setCollapsedSections(SectionMask mask) {
  if (mask == collapsed)
    return;

  collapsed = mask;

  // A hover held on a row that just folded away would keep the gutter caption
  // lit for a knob nobody can see.
  if (rowIsCollapsed(highlighted, collapsed))
    highlighted = kNoRow;

  resized();
  repaint();
}

void StripColumn::setScroll(int s) {
  if (s == scroll)
    return;

  scroll = s;
  resized();
  repaint();
}

void StripColumn::drawFaderAt(int y) {
  auto &volume = fader();
  const auto track = volume.getBounds();

  if (track.getHeight() <= 1)
    return;

  // Top of the track is full, bottom is nothing, and anything past either end
  // is that end.
  const auto fromTop = juce::jlimit(
      0.0, 1.0, (double)(y - track.getY()) / (double)track.getHeight());

  // Through the slider, whose range carries the parameter's skew.
  volume.setValue(volume.proportionOfLengthToValue(1.0 - fromTop),
                  juce::sendNotificationSync);
}

Row StripColumn::rowUnder(const RowBounds &rows, juce::Point<int> p) {
  const auto section = headingSectionAt(rows, p);

  return section != Section::NumSections ? sectionHeading(section)
                                         : controlRowAt(rows, p);
}

void StripColumn::mouseEnter(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);
}

void StripColumn::mouseMove(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);

  // The same hand the gutter's headings show, for the column's own events
  // only. Back to the parent's cursor rather than an arrow, so LINK and the
  // drawing tool can still set one across the whole mixer.
  if (e.originalComponent == this)
    setMouseCursor(headingSectionAt(currentRows(), e.getPosition()) !=
                           Section::NumSections
                       ? juce::MouseCursor::PointingHandCursor
                       : juce::MouseCursor::ParentCursor);
}

void StripColumn::reportHover(const juce::MouseEvent &e) {
  const auto p = e.getEventRelativeTo(this).getPosition();
  const auto inside = getLocalBounds().contains(p) && !hoverSuppressed;

  // Worked out from where the pointer is now, since leaving one knob for the
  // next fires the exit before the enter. A rule between sections answers
  // with its own heading, since it is clickable.
  const auto row = inside ? rowUnder(currentRows(), p) : kNoRow;
  hover.hoverChanged(hoverIndex(), isOwnRow(row) ? kNoRow : row);

  // The column highlight, worked out from the pointer for the same reason.
  if (inside != hovered) {
    hovered = inside;
    repaint();
  }
}

void StripColumn::clearHover() {
  // Held until the pointer moves again. The exit that comes as a menu opens
  // carries a position inside this column and would light it back up.
  hoverSuppressed = true;

  hover.hoverChanged(hoverIndex(), kNoRow);

  if (hovered) {
    hovered = false;
    repaint();
  }
}

void StripColumn::paintOverChildren(juce::Graphics &g) {
  if (rowShowsHighlight(highlighted) && isHeadingRow(highlighted) &&
      !isOwnRow(highlighted))
    paintRowHighlight(g, currentRows()[(size_t)highlighted]);

  if (hovered)
    paintColumnHighlight(g, getLocalBounds());
}

void StripColumn::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows = currentRows();

  // Only the two bands that changed are repainted, not the whole column.
  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

void StripColumn::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;

  // Turns the controls' lights off rather than dimming the whole column, so
  // the mute and solo stay readable.
  std::function<void(juce::Component &)> drain = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      // Not the mute and solo pair.
      if (dynamic_cast<MuteSoloButton *>(child) != nullptr)
        continue;

      // Sliders and the waveform displays carry colour of their own. The meters
      // and the lamps get nothing to show on a silenced channel.
      if (dynamic_cast<juce::Slider *>(child) != nullptr ||
          dynamic_cast<ShapeButton *>(child) != nullptr) {
        child->getProperties().set("unlit", silenced);
        child->repaint();
      }

      drain(*child);
    }
  };

  drain(*this);

  silenceChanged();
  repaint();
}

juce::Colour StripColumn::backdropAt(int y) const {
  return channelBackgroundAt(colours::channel, y, getHeight());
}

juce::String StripColumn::levelReading(float gain) {
  // A fader all the way down reads -inF. Everything above counts down to
  // params::kQuietestLevelDb, which is as far as the cells reach.
  if (params::isSilentGain(gain))
    return "-inF";

  const auto db = params::levelDecibels(gain);

  return (db >= 0.0f ? "" : "-") + juce::String(std::abs(db), 1);
}

} // namespace ovt::ui
