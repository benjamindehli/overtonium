#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
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
  void resized() override;

  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

  void setSilencedByOthers(bool shouldDim);
  void setMeterLevel(float level) { meter.push(level); }

  /// Takes part in the row highlight, so the band crosses the noise channel
  /// too. LINK never reaches it, so there is nothing here to arm.
  void setHighlightedRow(Row);

private:
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
      release, amRate, amDepth, velocity, aftertouch, pan, volume;
  juce::TextButton muteButton{"M"}, soloButton{"S"};
  juce::Label colourReadout, levelReadout;
  LevelMeter meter;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;
  Row highlighted = kNoRow;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseStrip)
};

} // namespace ovt::ui
