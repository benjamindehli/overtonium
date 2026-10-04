#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "MuteSoloButton.h"
#include "ShapeButton.h"
#include "Theme.h"

namespace ovt::ui {

/// The noise channel.
///
/// It shares the row layout of the partial strips so the gutter labels line up,
/// but it has no pitch, so the tuning, pitch modulation and drift rows are left
/// empty. Colour takes the tuning row: it tilts the noise from dark through
/// flat to bright.
class NoiseStrip : public juce::Component, public juce::SettableTooltipClient {
public:
  NoiseStrip(juce::AudioProcessorValueTreeState &state,
             HoverTarget &hoverTarget, juce::Component &popupParent);

  void paint(juce::Graphics &) override;

  /// The channel highlight. See ChannelStrip::paintOverChildren for why it
  /// goes over the children rather than behind them.
  void paintOverChildren(juce::Graphics &) override;

  void resized() override;

  /// See ChannelStrip::setCollapsedSections. The noise channel shares the
  /// mixer's rows, so it folds with everything else.
  void setCollapsedSections(SectionMask);

  /// How far the parameters are scrolled, shared by every column.
  ///
  /// The gutter, the 32 strips and the noise channel are handed the same
  /// number by the editor, which is what keeps a caption pointing at the knob
  /// beside it. Snapped to a row boundary inside layoutRows, so a row is never
  /// half over the header.
  void setScroll(int);

  /// Asked for when a click lands on one of the rules between sections, which
  /// line up with the gutter's headings and do the same thing.
  std::function<void(Section)> onSectionToggled;

  /// Sets this channel's fader from a height, for a drag drawing across the
  /// mixer. It is not a harmonic, but it is a fader. See
  /// ChannelStrip::drawFaderAt.
  void drawFaderAt(int y);

  /// Lights this channel's fader while a drag across the mixer would draw it.
  /// LINK never reaches the noise channel, so it has no preview of its own and
  /// this is the only thing that lights its fader.
  void setDrawGlow(bool);

  void mouseDown(const juce::MouseEvent &) override;

  /// As TopBar::onLearnRequested. The noise channel is one strip rather than
  /// a series, so LINK has nothing to say about it either.
  std::function<void(const juce::String &)> onLearnRequested;
  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

  /// Keeps a wheel that landed on a control from scrolling the mixer as well.
  ///
  /// The strip listens to everything inside it, so that a pointer resting on a
  /// knob is reported by the strip rather than swallowed by the control. JUCE
  /// hands that listener every event, wheels included, and Component's own
  /// handler passes whatever it is given up to the parent. So a scroll the
  /// knob had already taken went on to the viewport and dragged the series
  /// sideways under the hand that was turning the knob, whenever the window
  /// was narrow enough for there to be anything to scroll.
  ///
  /// Only a wheel that actually landed on the strip is passed on, which leaves
  /// the background scrolling the series and a control keeping its own.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  void setSilencedByOthers(bool shouldDim);
  /// @returns the region of this strip that needs redrawing. See
  /// ChannelStrip::setMeterLevel.
  juce::Rectangle<int> setMeterLevel(float level) {
    const auto band = meter.push(level);
    return band.isEmpty() ? band : band.translated(meter.getX(), meter.getY());
  }

  /// The lamps on the section rules. Noise has an envelope and a tremolo like
  /// any other channel, so it gets those two. It has no pitch, so the pitch
  /// rule stays a plain rule, which is the same thing the "no pitch" label
  /// above it is saying. See ChannelStrip::setActivity.
  void setActivity(float envelope, float tremolo, float touch,
                   juce::Array<juce::Rectangle<int>> &into);

  /// Whether the pointer is on this channel, which is what lights the column.
  bool isHovered() const noexcept { return hovered; }

  /// Takes part in the row highlight, so the band crosses the noise channel
  /// too. LINK never reaches it, so there is nothing here to arm.
  void setHighlightedRow(Row);

private:
  void updateLevelReadout();

  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void setUpKnob(juce::Slider &, const char *suffix, juce::Colour fill,
                 const juce::String &tooltip);

  void reportHover(const juce::MouseEvent &);

  /// Lets go of the hover without a mouse event to say so, for when a modal
  /// menu is about to take the pointer away.
  void clearHover();

  juce::AudioProcessorValueTreeState &apvts;
  HoverTarget &hover;
  juce::Component &popupHost;

  const juce::Colour colour;

  juce::Slider colourKnob, strike, delay, attack, decay, sustain, swell,
      offLevel, release, amRate, amDepth, velocity, aftertouch, pan, volume;
  /// The tremolo's shape. No pitch modulator here to give one to.
  ShapeButton amShape;

  MuteSoloButton muteButton, soloButton;
  /// COLOUR is a word rather than a figure, so it stays a label. The level is
  /// the same reading a partial's is and is drawn the same way.
  juce::Label colourReadout;
  SegmentDisplay levelReadout{{}};
  LevelMeter meter;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp, touchLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;
  Row highlighted = kNoRow;

  /// Folded groups, set by the editor. See ChannelStrip.
  SectionMask collapsed = 0;
  int scroll = 0;

  /// Hides a row that has scrolled under the pinned header. See HeaderCap.
  HeaderCap headerCap;

  void paintHeaderBand(juce::Graphics &);
  bool hovered = false;

  /// Set when a menu takes the pointer away, and cleared when the pointer
  /// moves under its own steam again. See clearHover.
  bool hoverSuppressed = false;

  /// When the last click on this strip happened, so the same click arriving a
  /// second time cannot act twice. See mouseDown.
  juce::Time lastClick;

  bool drawGlow = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseStrip)
};

} // namespace ovt::ui
