#pragma once

#include <array>
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
class NoiseStrip : public StripColumn {
public:
  NoiseStrip(juce::AudioProcessorValueTreeState &state,
             HoverTarget &hoverTarget, juce::Component &popupParent);

  void paint(juce::Graphics &) override;

  void resized() override;

  /// Lights this channel's fader while a drag across the mixer would draw it.
  void setDrawGlow(bool);

  void mouseDown(const juce::MouseEvent &) override;

  /// As TopBar::onLearnRequested, with no LINK items.
  std::function<void(const juce::String &)> onLearnRequested;
  void mouseExit(const juce::MouseEvent &) override;

  /// Keeps a wheel that landed on a control from scrolling the mixer as well.
  /// The strip listens deeply for hover, so it hears its controls' wheels too.
  /// Only a wheel that landed on the strip itself is passed up.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  /// @returns the region of this strip that needs redrawing. See
  /// ChannelStrip::setMeterLevel.
  juce::Rectangle<int> setMeterLevel(float level) {
    const auto band = meter.push(level);
    return band.isEmpty() ? band : band.translated(meter.getX(), meter.getY());
  }

  /// The lamps on the section rules, with none on the pitch rule. See
  /// ChannelStrip::setActivity.
  void setActivity(float envelope, float tremolo, float velGain, float keyGain,
                   float pressure, juce::Array<juce::Rectangle<int>> &into);

  /// For tests: the amplitude modulator's shape and its rate.
  ShapeButton &getAmShape() { return amShape; }
  const juce::Slider &getAmRate() const { return amRate; }

private:
  void updateLevelReadout();

  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void setUpKnob(juce::Slider &, const char *suffix, juce::Colour fill,
                 const juce::String &tooltip, const char *title = nullptr);

  /// Outside the harmonic series, so no LINK preview.
  int hoverIndex() const override { return -1; }
  juce::Slider &fader() override { return volume; }
  void silenceChanged() override;

  /// Colour on the tuning rows and the string on the pitch modulation rows.
  bool isOwnRow(Row) const override;

  juce::AudioProcessorValueTreeState &apvts;
  juce::Component &popupHost;

  const juce::Colour colour;

  juce::Slider colourKnob, strike, delay, attack, decay, sustain, swell,
      offLevel, release, amRate, amDepth, velocity, keyboard, aftertouch, pan,
      volume;

  /// The string the noise strikes, in the rows a partial's pitch modulation
  /// takes. Each has its own caption, since the gutter names those rows for
  /// the partials.
  juce::Slider stringAmount, stringDecay, stringDamp, stringPoint;
  std::array<juce::Rectangle<int>, 4> stringCaptions{};
  /// The tremolo's shape.
  ShapeButton amShape;

  MuteSoloButton muteButton, soloButton;
  /// COLOUR is a word, so it is a label. The level is drawn as a partial's is.
  juce::Label colourReadout;
  SegmentDisplay levelReadout{{}};
  LevelMeter meter;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp;
  SplitLamp outputLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  /// Hides a row that has scrolled under the pinned header. See HeaderCap.
  HeaderCap headerCap;

  void paintHeaderBand(juce::Graphics &);

  /// When the last click on this strip happened, so the same click arriving a
  /// second time cannot act twice. See mouseDown.
  juce::Time lastClick;

  bool drawGlow = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseStrip)
};

} // namespace ovt::ui
