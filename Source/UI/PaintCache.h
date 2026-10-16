#pragma once

#include <array>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ovt::ui {

/// Adds up everything a control's look depends on into one number.
///
/// FNV-1a, which is plenty to tell two looks of the same control apart.
class LookKey {
public:
  LookKey &add(double v) { return bytes(&v, sizeof v); }
  LookKey &add(bool v) { return add(v ? 1.0 : 0.0); }
  LookKey &add(int v) { return add((double)v); }
  LookKey &add(juce::Colour c) {
    const auto argb = c.getARGB();
    return bytes(&argb, sizeof argb);
  }
  LookKey &add(const juce::String &t) {
    return bytes(t.toRawUTF8(), t.getNumBytesAsUTF8());
  }
  LookKey &add(const juce::NamedValueSet &set) {
    for (int i = 0; i < set.size(); ++i)
      add(set.getName(i).toString()).add(set.getValueAt(i).toString());
    return *this;
  }

  std::uint64_t value() const noexcept { return h; }

private:
  LookKey &bytes(const void *data, size_t size) {
    const auto *b = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i) {
      h ^= b[i];
      h *= 1099511628211ull;
    }
    return *this;
  }

  std::uint64_t h = 1469598103934665603ull;
};

/// The last two looks of one control, kept as pictures.
///
/// The mixer has 33 strips of the same few controls, and soloing, scrolling
/// or resizing redraws every one of them. Most of them look exactly as they
/// did the last time, so the drawing is done once and copied after that.
/// Two looks rather than one because soloing swaps each control between its
/// lit and its unlit look, and both should be ready.
///
/// Held by the control itself, so the pictures go with its window. Nothing
/// here may be static: see the notes on the grain tile in LookAndFeel.cpp.
class PaintCache {
public:
  /// Draws `component` from a picture, or with `draw` if nothing matches
  /// `key`. The key has to cover everything `draw` reads apart from the size
  /// and the display scale, which are added here.
  ///
  /// @returns which of the two pictures was used, so a control that works
  ///          something out as it draws can keep that alongside it.
  template <typename Draw>
  size_t paint(juce::Graphics &g, const juce::Component &component, LookKey key,
               Draw &&draw) {
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto bounds = component.getLocalBounds();
    const auto k =
        key.add(bounds.getWidth()).add(bounds.getHeight()).add(scale).value();

    for (size_t i = 0; i < looks.size(); ++i)
      if (looks[i].key == k && looks[i].picture.isValid()) {
        newest = i;
        g.drawImage(looks[i].picture, bounds.toFloat());
        return i;
      }

    // Drawn at the display's own resolution, so the picture is copied pixel
    // for pixel rather than scaled.
    const auto w =
        juce::jmax(1, juce::roundToInt((float)bounds.getWidth() * scale));
    const auto h =
        juce::jmax(1, juce::roundToInt((float)bounds.getHeight() * scale));
    juce::Image picture(juce::Image::ARGB, w, h, true);
    {
      juce::Graphics into(picture);
      into.addTransform(juce::AffineTransform::scale(
          (float)w / (float)juce::jmax(1, bounds.getWidth()),
          (float)h / (float)juce::jmax(1, bounds.getHeight())));
      draw(into);
    }

    // Over the older of the two.
    newest = 1 - newest;
    looks[newest] = {k, picture};
    g.drawImage(picture, bounds.toFloat());
    return newest;
  }

private:
  struct Look {
    std::uint64_t key = 0;
    juce::Image picture;
  };

  std::array<Look, 2> looks{};
  size_t newest = 0;
};

} // namespace ovt::ui
