#include "Presets.h"

#include <algorithm>
#include <cmath>

#include "PluginParameters.h"
#include "dsp/Harmonics.h"

namespace ovt::presets {

namespace {

using APVTS = juce::AudioProcessorValueTreeState;

struct Applier {
  APVTS &apvts;

  void set(const juce::String &id, float plainValue) const {
    if (auto *p = apvts.getParameter(id))
      p->setValueNotifyingHost(p->convertTo0to1(plainValue));
  }

  void osc(const char *suffix, int index0, float plainValue) const {
    set(params::oscParamId(suffix, index0), plainValue);
  }

  /// Applies fn(harmonicNumber) to every strip.
  template <typename Fn> void allOsc(const char *suffix, Fn &&fn) const {
    for (int i = 0; i < kNumHarmonics; ++i)
      osc(suffix, i, (float)fn(i + 1));
  }

  void resetAllToDefault() const {
    for (auto *p : apvts.processor.getParameters())
      if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p))
        ranged->setValueNotifyingHost(ranged->getDefaultValue());
  }

  /// Everything a preset does not explicitly set should start from a known
  /// state.
  void neutralBase() const {
    allOsc(params::tuneSuffix, [](int) { return 1.0; });
    allOsc(params::pmRateSuffix, [](int) { return 4.0; });
    allOsc(params::pmDepthSuffix, [](int) { return 0.0; });
    allOsc(params::attackSuffix, [](int) { return 0.005; });
    allOsc(params::decaySuffix, [](int) { return 0.6; });
    allOsc(params::sustainSuffix, [](int) { return 1.0; });
    allOsc(params::releaseSuffix, [](int) { return 0.4; });
    allOsc(params::amRateSuffix, [](int) { return 4.0; });
    allOsc(params::amDepthSuffix, [](int) { return 0.0; });
    allOsc(params::velSuffix, [](int) { return 0.7; });
    allOsc(params::atSuffix, [](int) { return 0.0; });
    allOsc(params::muteSuffix, [](int) { return 0.0; });
    allOsc(params::soloSuffix, [](int) { return 0.0; });
    allOsc(params::volumeSuffix, [](int) { return 0.0; });

    set(params::spreadId, 0.0f);
  }
};

const char *const kNames[] = {
    "Init",     "Drawbar Organ", "Struck Bell", "Slow Pad",   "Odd Harmonics",
    "Just Saw", "Equal Saw",     "Shimmer",     "Vibraphone",
};

} // namespace

juce::StringArray names() {
  juce::StringArray a;
  for (auto *n : kNames)
    a.add(n);

  return a;
}

void apply(APVTS &apvts, int index) {
  const Applier ap{apvts};

  switch (index) {
  case 0: // Init
    ap.resetAllToDefault();
    break;

  case 1: // Drawbar Organ
  {
    ap.neutralBase();

    // A registration in the spirit of 88 8000 000: octaves and fifths, no
    // sevenths.
    static const float levels[kNumHarmonics] = {
        1.00f, 0.85f, 0.70f, 0.60f, 0.00f, 0.45f, 0.00f, 0.35f,
        0.00f, 0.00f, 0.00f, 0.20f, 0.00f, 0.00f, 0.00f, 0.15f,
        0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.10f,
        0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.08f};

    for (int i = 0; i < kNumHarmonics; ++i)
      ap.osc(params::volumeSuffix, i, levels[i]);

    ap.allOsc(params::attackSuffix, [](int) { return 0.008; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.06; });
    break;
  }

  case 2: // Struck Bell
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix,
              [](int n) { return 0.9 / std::pow((double)n, 0.8); });
    ap.allOsc(params::attackSuffix, [](int) { return 0.001; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    // Higher partials die away first, which is what makes a struck body sound
    // struck.
    ap.allOsc(params::decaySuffix,
              [](int n) { return 6.0 / (1.0 + 0.35 * (n - 1)); });
    ap.allOsc(params::releaseSuffix,
              [](int n) { return 6.0 / (1.0 + 0.35 * (n - 1)); });
    // Strike it harder and the upper partials arrive, the way a real bar or
    // string brightens with force.
    ap.allOsc(params::velSuffix,
              [](int n) { return std::min(1.0, 0.2 + 0.06 * (n - 1)); });
    break;
  }

  case 3: // Slow Pad
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix,
              [](int n) { return n <= 16 ? 1.0 / n : 0.0; });
    // Staggered attacks: the spectrum opens up over a couple of seconds.
    ap.allOsc(params::attackSuffix, [](int n) { return 0.6 + 0.05 * (n - 1); });
    ap.allOsc(params::decaySuffix, [](int) { return 4.0; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.8; });
    ap.allOsc(params::releaseSuffix, [](int) { return 3.0; });
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.15; });
    ap.allOsc(params::amRateSuffix, [](int n) { return 0.3 + 0.07 * n; });

    ap.set(params::spreadId, 0.7f);
    break;
  }

  case 4: // Odd Harmonics
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix,
              [](int n) { return (n % 2) ? 1.0 / n : 0.0; });
    ap.allOsc(params::attackSuffix, [](int) { return 0.02; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.2; });
    ap.allOsc(params::velSuffix,
              [](int n) { return std::min(1.0, 0.3 + 0.05 * (n - 1)); });
    break;
  }

  case 5: // Just Saw
  case 6: // Equal Saw
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix, [](int n) { return 1.0 / n; });
    ap.allOsc(params::attackSuffix, [](int) { return 0.003; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.15; });

    // The pair exists to be A/B'd: identical but for the tuning of every
    // partial.
    const float blend = (index == 5) ? 1.0f : 0.0f;
    ap.allOsc(params::tuneSuffix, [blend](int) { return blend; });
    break;
  }

  case 7: // Shimmer
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix, [](int n) {
      return n == 1 ? 0.6 : (n >= 8 ? 0.5 / std::sqrt((double)n) : 0.0);
    });
    ap.allOsc(params::attackSuffix, [](int) { return 1.5; });
    ap.allOsc(params::releaseSuffix, [](int) { return 4.0; });
    // Every partial breathes at its own rate, so the spectrum never repeats.
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.5; });
    ap.allOsc(params::amRateSuffix, [](int n) { return 0.15 + 0.05 * n; });
    ap.allOsc(params::pmDepthSuffix, [](int) { return 4.0; });
    ap.allOsc(params::pmRateSuffix, [](int n) { return 0.2 + 0.03 * n; });

    ap.set(params::spreadId, 1.0f);
    break;
  }

  case 8: // Vibraphone
  {
    ap.neutralBase();

    ap.allOsc(params::attackSuffix, [](int) { return 0.002; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });

    // The bar's three strongest modes, plus the motor.
    const int partials[3] = {1, 4, 10};
    const float levels[3] = {1.0f, 0.5f, 0.25f};
    const float decays[3] = {3.0f, 1.5f, 0.8f};

    for (int k = 0; k < 3; ++k) {
      const int i = partials[k] - 1;

      ap.osc(params::volumeSuffix, i, levels[k]);
      ap.osc(params::decaySuffix, i, decays[k]);
      ap.osc(params::releaseSuffix, i, decays[k]);
      ap.osc(params::amDepthSuffix, i, 0.6f);
      ap.osc(params::amRateSuffix, i, 5.0f);
    }
    break;
  }

  default:
    jassertfalse;
    break;
  }
}

} // namespace ovt::presets
