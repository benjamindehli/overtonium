#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MuteSoloButton.h"
#include "ShapeButton.h"
#include "StripColumn.h"
#include "Theme.h"

namespace ovt::ui {

/// One long lamp across a rule, in a slot like the needle's, split into a part
/// for each row under the rule and in the same order. For the OUTPUT rule,
/// whose three rows would leave three round lamps too little room each for
/// their glow.
///
/// Each part is lit on its own, quantised like an ActivityLamp.
class SplitLamp : public juce::Component {
public:
  static constexpr int kParts = 3;

  explicit SplitLamp(juce::Colour litColour) : colour(litColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// See LevelMeter::setBackdrop.
  void setBackdrop(juce::Colour);

  /// @param brightness  0 to 1 for each part, left to right.
  /// @returns true when any part moved a step and needs repainting.
  bool push(const std::array<float, kParts> &brightness);

  /// How many steps a part is lit, 0 to ActivityLamp::kSteps, for reading
  /// back what the lamp shows.
  int stepOf(int part) const noexcept {
    return part >= 0 && part < kParts ? steps[(size_t)part] : 0;
  }

  void paint(juce::Graphics &) override;

  /// The margin the slot leaves itself inside the component, each side, so a
  /// lit outer part has room for its glow.
  static constexpr float kSlotInset = 2.0f;

private:
  juce::Colour colour, backdrop;
  std::array<int, kParts> steps{};
};

/// A needle on a rule, showing where pitch modulation, drift and a glide still
/// on its way have this partial.
///
/// Centre is the note as written, right is sharp, left is flat, on a fixed
/// scale that is the same on every strip.
class ActivityNeedle : public juce::Component {
public:
  explicit ActivityNeedle(juce::Colour needleColour) : colour(needleColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// See LevelMeter::setBackdrop.
  void setBackdrop(juce::Colour);

  /// The margin the slot leaves itself inside the component, each side.
  ///
  /// One pixel for the recess lip, which is drawn outside the opening. The
  /// travel is measured off the same number.
  static constexpr int kSlotInset = 1;

  /// @param position  -1 to 1, flat to sharp, already scaled by the caller.
  /// @returns true when the needle moved a pixel and needs repainting.
  bool push(float position);

  /// Whether the travel is marked at its middle. Off for a reading that rests
  /// at the left end.
  void showCentre(bool);

  /// Where the needle sits, in pixels from the left edge, or -1 for parked.
  /// Read back by the tests.
  int getColumn() const noexcept { return column; }

  void paint(juce::Graphics &) override;

private:
  /// Where the needle sits, in pixels from the left edge, or -1 for parked.
  /// Quantised to the pixel, so it only repaints when it moves a pixel.
  int column = -1;
  bool centreMark = true;

  juce::Colour colour, backdrop;
};

/// One vertical channel: everything that belongs to a single partial.
class ChannelStrip : public StripColumn {
public:
  ChannelStrip(juce::AudioProcessorValueTreeState &state,
               LinkTarget &linkTarget, HoverTarget &hoverTarget,
               juce::Component &popupParent, int index0);

  void paint(juce::Graphics &) override;

  void resized() override;

  void mouseExit(const juce::MouseEvent &) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;

  /// Asks the editor to scroll, and says whether it had anywhere to go.
  bool scrollParametersBy(const juce::MouseWheelDetails &);

  /// Passes on only a wheel that landed on the strip itself, so a wheel a
  /// control has taken does not scroll the mixer as well.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  /// @returns the region of this strip that needs redrawing, empty if the
  /// meter did not move. The editor merges these and invalidates once.
  juce::Rectangle<int> setMeterLevel(float level) {
    const auto band = meter.push(level);
    return band.isEmpty() ? band : band.translated(meter.getX(), meter.getY());
  }

  /// What the lamps on the section rules show.
  ///
  /// @param envelope  signed: positive while the key is down, negative once
  ///                  the key-off stage has it.
  /// @param tremolo   how far the tremolo has pulled the level down, 0 to 1.
  /// @param pitch     displacement in cents, or the parked value when the
  ///                  partial is silent.
  ///
  /// Appends the bounds of every lamp that moved, in this strip's own
  /// coordinates, for the editor to merge with the meter bands. Too many
  /// separate invalidations make the window redraw their whole bounding box.
  void setActivity(float envelope, float tremolo, float pitch, float velGain,
                   float keyGain, float pressure,
                   juce::Array<juce::Rectangle<int>> &into);

  /// The lamp on the OUTPUT rule, for reading back what it shows.
  const SplitLamp &getOutputLamp() const noexcept { return outputLamp; }

  /// Where a displacement sits on the needle's travel, -1 to 1.
  ///
  /// Full deflection is always params::kPitchNeedleFullScaleCents.
  static float needlePosition(float cents);

  /// Arms the control LINK would move on this strip.
  ///
  /// @param amount  0 for a strip the drag does not reach, otherwise how much
  ///                of the drag it takes relative to the strip that takes most.
  void setLinkGlow(Role, float amount, bool accent = false);

  /// How much of a LINK drag this channel would take, 0 to 1, for the number
  /// at the head of the strip.
  ///
  void setLinkReach(float amount);

  /// Whether this channel is in the selection the Selected scope reaches.
  /// Marked under the number, apart from the LINK reach, which lights the
  /// number itself.
  void setSelected(bool);
  bool isSelected() const { return selected; }

  /// Which macro drives this row, as its colour, or transparent for none.
  ///
  /// The ring goes to the macro's colour while the pointer keeps the
  /// channel's.
  ///
  /// @param result  where the row ends up once the macro has had its say, as
  ///                a proportion of the control's travel.
  void setMacroTint(Role, juce::Colour, float result);

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void setUpKnob(LinkableSlider &, Role, juce::Colour fill);
  void setUpFader(LinkableSlider &, Role, juce::Colour fill);
  void wireUp(LinkableSlider &, Role);

  void updateTuneReadout();
  void updateLevelReadout();

  int hoverIndex() const override { return index; }
  juce::Slider &fader() override { return volume; }
  void silenceChanged() override;

  LinkableSlider *sliderForRole(Role);

  /// The colour each control wears when no macro has it, kept so that one
  /// letting go puts the channel's own back rather than an approximation.
  std::array<juce::Colour, (size_t)kNumRoles> baseColour{};

  /// Which macro colour each row is wearing now, so a repaint only happens
  /// when one actually changes.
  std::array<juce::Colour, (size_t)kNumRoles> macroTint{};

  /// And where the macro has taken that row, for the same reason.
  std::array<float, (size_t)kNumRoles> macroResult{};

  juce::AudioProcessorValueTreeState &apvts;
  LinkTarget &link;
  juce::Component &popupHost;

  const int index;
  const HarmonicInfo info;
  const juce::Colour colour;

  LinkableSlider tune, glide, pmRate, pmDepth, drift, strike, delay, attack,
      decay, sustain, swell, offLevel, release, amRate, amDepth, velocity,
      keyboard, aftertouch, pan, volume;
  /// What each modulator traces.
  ShapeButton pmShape, amShape;

  MuteSoloButton muteButton, soloButton;
  /// No unit on either, since the gutter caption beside them says it.
  SegmentDisplay tuneReadout{{}}, levelReadout{{}};
  LevelMeter meter;

  ActivityNeedle pitchLamp;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp;

  /// VELOCITY, KEYBOARD and AFTERTOUCH, in one slot on the OUTPUT rule.
  SplitLamp outputLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  /// What hides a row that has scrolled under the pinned header. See HeaderCap.
  HeaderCap headerCap;

  void paintHeaderBand(juce::Graphics &);

  /// When the last click on this strip happened, so the same click arriving a
  /// second time cannot act twice. See mouseDown.
  juce::Time lastClick;

  void foldSectionUnder(const juce::MouseEvent &, bool echo);
  Role glowRole = Role::Tune;
  float glowAmount = 0.0f;

  /// See setLinkReach. Quantised before it is acted on, so a curve sliding
  /// under the pointer does not repaint thirty-two headers every frame.
  float linkReach = 0.0f;

  /// See setSelected, and whether a selection drag began on this strip.
  bool selected = false;
  bool selecting = false;

  /// Whether the glow is lit in the accent, for drawing, rather than in the
  /// channel's own colour, for LINK.
  bool glowAccent = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

} // namespace ovt::ui
