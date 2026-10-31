#include "UpdateCheck.h"

// For MessageManager::callAsync. Kept out of the header so it stays on
// juce_core alone.
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace ovt {

namespace {
/// Splits a dotted version into its numbers. An empty result means the string
/// was not a version.
juce::Array<int> parts(const juce::String &raw) {
  juce::Array<int> out;
  auto text = raw.trim();

  if (text.startsWithIgnoreCase("v"))
    text = text.substring(1);

  for (const auto &piece : juce::StringArray::fromTokens(text, ".", "")) {
    if (piece.isEmpty() || !piece.containsOnly("0123456789"))
      return {};

    out.add(piece.getIntValue());
  }

  return out;
}
} // namespace

bool isNewerVersion(const juce::String &candidate,
                    const juce::String &current) {
  const auto a = parts(candidate);
  const auto b = parts(current);

  if (a.isEmpty() || b.isEmpty())
    return false;

  for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i) {
    // A missing component is zero.
    const int x = i < a.size() ? a[i] : 0;
    const int y = i < b.size() ? b[i] : 0;

    if (x != y)
      return x > y;
  }

  return false;
}

std::optional<ReleaseInfo> parseReleaseJson(const juce::String &json) {
  const auto parsed = juce::JSON::parse(json);

  if (!parsed.isObject())
    return std::nullopt;

  ReleaseInfo info;
  info.version = parsed.getProperty("version", {}).toString().trim();
  info.url = parsed.getProperty("url", {}).toString().trim();

  if (info.version.isEmpty())
    return std::nullopt;

  // The link is opened in a browser, so it may only lead to this project's
  // pages on GitHub, and a ".." could climb out of them. Anything else, or
  // none, falls back to the releases page.
  if (!info.url.startsWith(kReleasePages) || info.url.contains(".."))
    info.url = kLatestRelease;

  return info;
}

namespace {
/// Where the settings file is, rather than the file itself.
///
/// Each caller builds a PropertiesFile, uses it and lets it go. A
/// PropertiesFile is a juce::Timer, and holding one in a static deadlocks the
/// host when the binary unloads. A plugin must not keep anything in a static
/// that owns a thread or needs the event system to be destroyed.
juce::PropertiesFile::Options settingsOptions() {
  juce::PropertiesFile::Options o;
  o.applicationName = "Overtonium";
  o.folderName = "Dehli Musikk/Overtonium";
  o.filenameSuffix = "settings";
  o.osxLibrarySubFolder = "Application Support";
  return o;
}

const char *const kAllowed = "updateCheckAllowed";
const char *const kOffered = "updateCheckOffered";
const char *const kAnnounced = "announcedVersion";

/// Reads one flag, with the file open for no longer than that.
bool flag(const char *key) {
  juce::PropertiesFile file(settingsOptions());
  return file.getBoolValue(key, false);
}

/// Writes one flag and puts it on disk before returning.
void setFlag(const char *key, bool value) {
  juce::PropertiesFile file(settingsOptions());
  file.setValue(key, value);
  file.saveIfNeeded();
}
} // namespace

bool updateCheckAllowed() { return flag(kAllowed); }

void setUpdateCheckAllowed(bool allowed) { setFlag(kAllowed, allowed); }

bool updateCheckOffered() { return flag(kOffered); }

void markUpdateCheckOffered() { setFlag(kOffered, true); }

void setUpdateCheckOffered(bool offered) { setFlag(kOffered, offered); }

juce::String announcedVersion() {
  juce::PropertiesFile file(settingsOptions());
  return file.getValue(kAnnounced);
}

void markVersionAnnounced(const juce::String &version) {
  juce::PropertiesFile file(settingsOptions());
  file.setValue(kAnnounced, version);
  file.saveIfNeeded();
}

UpdateCheck::UpdateCheck() : juce::Thread("Overtonium update check") {}

UpdateCheck::~UpdateCheck() {
  // The wait must outlast the fetch's five second timeout. If it expires,
  // stopThread kills the thread, which can leave allocator or socket locks
  // held and hang the message thread. In practice cancel() has been called
  // first and the fetch stops within one read.
  stopThread(10000);
}

void UpdateCheck::cancel() { signalThreadShouldExit(); }

void UpdateCheck::setListener(std::function<void()> fn) {
  const juce::ScopedLock sl(lock);
  listener = std::move(fn);
}

void UpdateCheck::start(const juce::String &currentVersion) {
  if (isThreadRunning())
    return;

  current = currentVersion;
  startThread(juce::Thread::Priority::background);
}

std::optional<ReleaseInfo> UpdateCheck::newerRelease() const {
  const juce::ScopedLock sl(lock);
  return found;
}

void UpdateCheck::run() {
  const juce::URL url(kFeedUrl);

  auto stream = url.createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(5000)
          .withNumRedirectsToFollow(3));

  if (stream == nullptr || threadShouldExit())
    return;

  // Read in pieces, so the memory held is bounded and cancel() takes effect
  // within one chunk.
  juce::MemoryOutputStream body;

  while (body.getDataSize() < kMaxFeedBytes) {
    if (threadShouldExit())
      return;

    char chunk[1024];
    const auto got = stream->read(chunk, static_cast<int>(sizeof(chunk)));

    if (got <= 0)
      break;

    body.write(chunk, static_cast<size_t>(got));
  }

  if (threadShouldExit())
    return;

  const auto info = parseReleaseJson(body.toString());

  if (!info.has_value() || !isNewerVersion(info->version, current))
    return;

  {
    const juce::ScopedLock sl(lock);
    found = info;
  }

  // Taken under the lock, since the message thread may replace it while this
  // runs. Nothing joins this thread on the way out, so the listener may be
  // called after whoever set it has gone.
  std::function<void()> callback;

  {
    const juce::ScopedLock sl(lock);
    callback = listener;
  }

  if (callback != nullptr)
    juce::MessageManager::callAsync([callback] { callback(); });
}

} // namespace ovt
