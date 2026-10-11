#include "OptionBars.h"

#include "../PluginParameters.h"
#include "LookAndFeel.h"
#include "SettingsMenus.h"

namespace ovt::ui {

namespace {
/// The same margins and spacing the top bar keeps, so the groups in a bar
/// read as more of it.
constexpr int kBarMargin = 12;
constexpr int kBarPadY = 6;
constexpr int kGroupGap = 6;
constexpr int kGroupPad = 6;
constexpr int kControlGap = 6;
constexpr int kControlHeight = 24;

/// Room for the bar's name, the way the wordmark has room in the top bar.
constexpr int kNameWidth = 62;

/// Short enough for the cells the displays have.
constexpr const char *kAftertouchWords[] = {"PRESSURE", "WHEEL", "EITHER"};
constexpr const char *kSlideWords[] = {"OFF", "BRIGHT", "TUNING"};
} // namespace

OptionBar::OptionBar(juce::String nameToShow) : name(std::move(nameToShow)) {
  // Shouted on the panel like everything else, and said normally to a screen
  // reader.
  setTitle(name.substring(0, 1) + name.substring(1).toLowerCase());
}

void OptionBar::addGroup(std::vector<Item> items) {
  for (auto &item : items)
    addAndMakeVisible(item.control);

  groups.push_back(std::move(items));
}

void OptionBar::resized() {
  groupBounds.clear();

  auto area = getLocalBounds().reduced(kBarMargin, kBarPadY);
  area.removeFromLeft(kNameWidth);

  for (auto &group : groups) {
    int width = 2 * kGroupPad;
    for (size_t i = 0; i < group.size(); ++i)
      width += group[i].width + (i > 0 ? kControlGap : 0);

    // A group that does not fit is left out whole rather than cut, and so is
    // everything after it. The Settings menu still has all of it.
    const bool fits = width <= area.getWidth();

    auto bounds = fits ? area.removeFromLeft(width) : juce::Rectangle<int>();
    groupBounds.push_back(bounds);
    area.removeFromLeft(kGroupGap);

    auto inner = bounds.reduced(kGroupPad, 0);

    for (size_t i = 0; i < group.size(); ++i) {
      auto &item = group[i];
      item.control->setVisible(fits);

      if (!fits)
        continue;

      if (i > 0)
        inner.removeFromLeft(kControlGap);

      // Centred on the line the top bar's dials stand on, with the caption in
      // the band its knobs put theirs in.
      const auto column = inner.removeFromLeft(item.width);
      const auto dial = LabelledKnob::dialBounds(column);

      item.control->setBounds(column.getX(),
                              dial.getCentreY() - kControlHeight / 2,
                              column.getWidth(), kControlHeight);
    }

    if (!fits)
      area = {};
  }
}

void OptionBar::paint(juce::Graphics &g) {
  // A shade under the top bar, so the two read as one panel in two layers
  // rather than as one bar that has grown.
  g.setColour(colours::panel.darker(0.12f));
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, getHeight() - 1, getWidth(), 1);

  g.setColour(colours::textDim);
  g.setFont(makeFont(10.0f, true));
  g.drawText(
      name,
      getLocalBounds().reduced(kBarMargin, kBarPadY).withWidth(kNameWidth),
      juce::Justification::centredLeft, false);

  g.setFont(makeFont(9.0f, true));

  for (size_t i = 0; i < groups.size() && i < groupBounds.size(); ++i) {
    const auto &bounds = groupBounds[i];

    if (bounds.isEmpty())
      continue;

    paintGroupPocket(g, bounds);

    // After the pocket, which leaves its own outline colour behind.
    g.setColour(colours::textDim);

    // Centred under each control, a little wider than it so a caption longer
    // than a narrow display still fits.
    for (const auto &item : groups[i])
      if (item.caption.isNotEmpty())
        g.drawText(item.caption,
                   LabelledKnob::captionBounds(bounds)
                       .withX(item.control->getX() - kControlGap)
                       .withWidth(item.control->getWidth() + 2 * kControlGap),
                   juce::Justification::centred, false);
  }
}

void OptionBar::showList(juce::Component &anchor, const juce::PopupMenu &list,
                         juce::AudioProcessorValueTreeState &apvts) {
  auto menu = list;
  menu.setLookAndFeel(&getLookAndFeel());

  juce::Component::SafePointer<OptionBar> self(this);

  menu.showMenuAsync(juce::PopupMenu::Options()
                         .withTargetComponent(&anchor)
                         .withStandardItemHeight(22),
                     [self, &apvts](int result) {
                       if (result != 0 && settings::apply(apvts, result) &&
                           self != nullptr)
                         self->refresh();
                     });
}

void OptionBar::styleSwitch(GlowButton &button, const juce::String &word,
                            const juce::String &title,
                            const juce::String &tooltip) {
  button.setButtonText(word);
  button.setTitle(title);
  button.setTooltip(tooltip);
  button.setColour(juce::TextButton::textColourOnId, colours::accent);
}

// ---- Playing ----------------------------------------------------------------

PlayingBar::PlayingBar(juce::AudioProcessorValueTreeState &state)
    : OptionBar("PLAYING"), apvts(state) {
  styleSwitch(legato, "LEGATO", "Legato",
              "A key going down while another is held carries the note on "
              "rather than starting a new one");
  styleSwitch(onePerKey, "1 PER KEY", "One voice per key",
              "A key played again takes over its own voice rather than "
              "starting another");
  styleSwitch(mpe, "MPE", "MPE",
              "Each note bent, pressed and slid on a channel of its own");

  const auto flip = [this](settings::Id id) {
    settings::apply(apvts, id);
    refresh();
  };

  legato.onClick = [flip] { flip(settings::kLegato); };
  onePerKey.onClick = [flip] { flip(settings::kOneVoicePerKey); };
  mpe.onClick = [flip] { flip(settings::kMpe); };

  voices.setTooltip("How many notes can sound at once");
  bend.setTooltip("How far the pitch wheel bends, in semitones");
  aftertouch.setTooltip("Which control the AT row on every channel follows");
  slide.setTooltip("What sliding a finger along an MPE key moves");

  voices.onClick = [this] { showList(voices, settings::voices(apvts), apvts); };
  bend.onClick = [this] { showList(bend, settings::bendRange(apvts), apvts); };
  aftertouch.onClick = [this] {
    showList(aftertouch, settings::aftertouchFrom(apvts), apvts);
  };
  slide.onClick = [this] { showList(slide, settings::slideTo(apvts), apvts); };

  addGroup({{&voices, 40, "VOICES"}, {&legato, 62, {}}, {&onePerKey, 74, {}}});
  addGroup({{&bend, 40, "BEND"}});
  addGroup(
      {{&mpe, 44, {}}, {&aftertouch, 96, "AFTERTOUCH"}, {&slide, 84, "SLIDE"}});

  refresh();
}

void PlayingBar::refresh() {
  const auto count = settings::voiceCount(apvts);
  voices.setReading(juce::String(count), true);
  voices.setTitle("Voices: " + juce::String(count));

  const auto range = settings::choiceIndex(apvts, params::bendRangeId);
  bend.setReading(juce::String(range), true);
  bend.setTitle("Pitch bend range: " + juce::String(range) + " semitones");

  const auto source =
      juce::jlimit(0, 2, settings::choiceIndex(apvts, params::atSourceId));
  aftertouch.setReading(kAftertouchWords[source], true);
  aftertouch.setTitle(juce::String("Aftertouch from: ") +
                      params::kAftertouchSourceNames[(size_t)source]);

  const auto target =
      juce::jlimit(0, 2, settings::choiceIndex(apvts, params::slideDestId));
  slide.setReading(kSlideWords[target], true);
  slide.setTitle("Slide to: " + params::slideDestChoices[target]);

  legato.setToggleState(settings::legatoOn(apvts), juce::dontSendNotification);
  onePerKey.setToggleState(settings::isOn(apvts, params::oneVoicePerKeyId),
                           juce::dontSendNotification);
  mpe.setToggleState(settings::isOn(apvts, params::mpeId),
                     juce::dontSendNotification);
}

// ---- Tuning -----------------------------------------------------------------

TuningBar::TuningBar() : OptionBar("TUNING") {}

} // namespace ovt::ui
