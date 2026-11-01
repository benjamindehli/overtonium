#pragma once

#include <functional>
#include <optional>

#include <juce_core/juce_core.h>

namespace ovt {

/// Whether `candidate` names a release later than `current`.
///
/// Both are dotted numbers, with or without a leading v. Missing components
/// count as zero. A string that does not parse is never newer.
bool isNewerVersion(const juce::String &candidate, const juce::String &current);

/// Whether the update check may run, and whether the offer to turn it on
/// has been made.
///
/// Kept machine-wide in a settings file beside the presets, not in the plugin
/// state, so new instances do not ask again.
bool updateCheckAllowed();
void setUpdateCheckAllowed(bool);

bool updateCheckOffered();
void markUpdateCheckOffered();

/// Clears the mark as well as setting it, so a test can restore the answer.
void setUpdateCheckOffered(bool);

/// The newest release already announced, so each is announced once. Empty
/// until the first is. Kept in the same settings file.
juce::String announcedVersion();
void markVersionAnnounced(const juce::String &version);

/// The one release the plugin asks about.
struct ReleaseInfo {
  juce::String version;
  juce::String url;
};

/// Where a release's link may lead, and where it goes instead.
inline constexpr const char *kReleasePages =
    "https://github.com/benjamindehli/overtonium/";
inline constexpr const char *kLatestRelease =
    "https://github.com/benjamindehli/overtonium/releases/latest";

/// Reads a release description out of the JSON the site publishes.
/// Returns nothing on anything unexpected. A link outside kReleasePages is
/// replaced by kLatestRelease.
std::optional<ReleaseInfo> parseReleaseJson(const juce::String &json);

/// Asks the project page whether there is a newer release.
///
/// Nothing runs and no socket is opened until `start` is called, which the
/// editor only does when the setting allows it. The request runs on its own
/// thread.
class UpdateCheck final : private juce::Thread {
public:
  UpdateCheck();
  ~UpdateCheck() override;

  /// Fetches in the background, unless a fetch is already running. The
  /// listener is called on the message thread only if a newer release is found.
  void start(const juce::String &currentVersion);

  /// Asks the fetch to give up, without waiting for it.
  ///
  /// For an editor closing: waiting on the message thread for a socket that is
  /// not answering would hang the window. The owner joins the thread later.
  void cancel();

  /// The newer release, if the last check found one.
  std::optional<ReleaseInfo> newerRelease() const;

  /// Called on the message thread when a newer release is found.
  ///
  /// Guarded by the same lock as the result. Whatever it captures must survive
  /// being called after the editor that set it has gone.
  void setListener(std::function<void()>);

  /// Where the answer comes from: a static file the release workflow writes
  /// beside the documentation.
  static constexpr const char *kFeedUrl =
      "https://benjamindehli.github.io/overtonium/latest.json";

  /// The most of that file the plugin will read.
  static constexpr size_t kMaxFeedBytes = 4096;

private:
  void run() override;

  juce::String current;

  mutable juce::CriticalSection lock;
  std::optional<ReleaseInfo> found;
  std::function<void()> listener;
};

} // namespace ovt
