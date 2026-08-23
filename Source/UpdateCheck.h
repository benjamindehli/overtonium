#pragma once

#include <functional>
#include <optional>

#include <juce_core/juce_core.h>

namespace ovt {

/// Whether `candidate` names a release later than `current`.
///
/// Both are dotted numbers, with or without a leading v. Missing components
/// count as zero, so 1.1 is later than 1.0.9 and the same as 1.1.0. Anything
/// that does not parse as a number stops the comparison, which means a string
/// that is not a version cannot read as an upgrade.
bool isNewerVersion(const juce::String &candidate, const juce::String &current);

/// The one release the plugin asks about.
struct ReleaseInfo {
  juce::String version;
  juce::String url;
};

/// Reads a release description out of the JSON the site publishes.
///
/// Returns nothing rather than throwing on anything unexpected, since the far
/// end is a file on a web server and the plugin has no business caring why it
/// was malformed.
std::optional<ReleaseInfo> parseReleaseJson(const juce::String &json);

/// Asks the project page whether there is a newer release.
///
/// Off unless asked. Nothing here runs, and no socket is opened, until
/// `start` is called, which the editor only does once the setting says so.
/// The request happens on its own thread and never touches the audio thread.
class UpdateCheck final : private juce::Thread {
public:
  UpdateCheck();
  ~UpdateCheck() override;

  /// Fetches in the background, unless a fetch is already running. `onResult`
  /// is called on the message thread if a newer release is found, and not
  /// called at all otherwise, since there is nothing to say when you are up to
  /// date.
  void start(const juce::String &currentVersion);

  /// The newer release, if the last check found one.
  std::optional<ReleaseInfo> newerRelease() const;

  std::function<void()> onResult;

  /// Where the answer comes from. A static file beside the documentation
  /// rather than the GitHub API: no rate limit, no dependency on a JSON shape
  /// somebody else owns, and the release workflow writes it, so it cannot fall
  /// out of step with what has actually shipped.
  static constexpr const char *kFeedUrl =
      "https://benjamindehli.github.io/overtonium/latest.json";

private:
  void run() override;

  juce::String current;

  mutable juce::CriticalSection lock;
  std::optional<ReleaseInfo> found;
};

} // namespace ovt
