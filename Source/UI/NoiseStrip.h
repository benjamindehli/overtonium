#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "MuteSoloButton.h"
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

  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

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
  void setActivity(float envelope, float tremolo,
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

  juce::AudioProcessorValueTreeState &apvts;
  HoverTarget &hover;
  juce::Component &popupHost;

  const juce::Colour colour;

  juce::Slider colourKnob, delay, attack, decay, sustain, swell, offLevel,
      lift,
      release, amRate, amDepth, velocity, aftertouch, pan, volume;
  MuteSoloButton muteButton, soloButton;
  /// COLOUR is a word rather than a figure, so it stays a label. The level is
  /// the same reading a partial's is and is drawn the same way.
  juce::Label colourReadout;
  SegmentDisplay levelReadout{{}};
  LevelMeter meter;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;
  Row highlighted = kNoRow;

  /// Folded groups, set by the editor. See ChannelStrip.
  SectionMask collapsed = 0;
  bool hovered = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseStrip)
};

} // namespace ovt::ui
