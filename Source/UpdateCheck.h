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

/// Whether the update check is allowed to run, and whether the offer to turn
/// it on has been made.
///
/// Machine-wide, in a settings file beside the presets, rather than in the
/// plugin's own state. A preference about reaching the network belongs to the
/// person, not to a patch or a session: kept in the state it would be asked
/// again by every new instance, which is what a host scanning its plugin
/// folder does dozens of times in a row.
bool updateCheckAllowed();
void setUpdateCheckAllowed(bool);

bool updateCheckOffered();
void markUpdateCheckOffered();

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

  /// Fetches in the background, unless a fetch is already running. The
  /// listener is called on the message thread if a newer release is found, and
  /// not called at all otherwise, since there is nothing to say when you are
  /// up to date.
  void start(const juce::String &currentVersion);

  /// Asks the fetch to give up, without waiting for it.
  ///
  /// For an editor closing. Nothing is left to show the answer to, so the work
  /// is pointless from here, but waiting for it is what must not happen: the
  /// call would be on the message thread, and a socket that is not answering
  /// would take the window with it. The thread is joined later, by whoever
  /// owns this, at a moment when a pause is nobody's problem.
  void cancel();

  /// The newer release, if the last check found one.
  std::optional<ReleaseInfo> newerRelease() const;

  /// Called on the message thread when a newer release is found.
  ///
  /// Set and cleared under the same lock the result uses, because the thread
  /// reads it and the message thread writes it. Whatever it captures has to
  /// survive being called after the thing that set it has gone, since a fetch
  /// outlives an editor: see how the editor sets it.
  void setListener(std::function<void()>);

  /// Where the answer comes from. A static file beside the documentation
  /// rather than the GitHub API: no rate limit, no dependency on a JSON shape
  /// somebody else owns, and the release workflow writes it, so it cannot fall
  /// out of step with what has actually shipped.
  static constexpr const char *kFeedUrl =
      "https://benjamindehli.github.io/overtonium/latest.json";

  /// The most of that file the plugin will hold. The feed is a couple of
  /// hundred bytes and the reply is read up to this and no further, so a server
  /// that answers with something enormous costs a wasted fetch rather than a
  /// plugin sitting on the memory.
  static constexpr size_t kMaxFeedBytes = 4096;

private:
  void run() override;

  juce::String current;

  mutable juce::CriticalSection lock;
  std::optional<ReleaseInfo> found;
  std::function<void()> listener;
};

} // namespace ovt
