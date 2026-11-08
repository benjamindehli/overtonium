#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ovt::ui {

/// The product wordmark, for the corner of the bar.
juce::Image logoWordmark();

/// A pointer that shows what a LINK drag would do: five bars beside the arrow
/// in the shape of the curve.
juce::MouseCursor linkCursor(LinkCurve);

/// The pencil pointer shown while a drag across the faders would draw them
/// rather than move one.
juce::MouseCursor drawCursor();

/// The artwork behind drawCursor, exposed so it can be looked at on its own.
juce::Image drawCursorImage(float scale);

/// The artwork behind linkCursor, exposed so it can be looked at on its own.
juce::Image linkCursorImage(LinkCurve, float scale);

} // namespace ovt::ui
