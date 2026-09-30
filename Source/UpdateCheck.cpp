#include "UpdateCheck.h"

// For MessageManager::callAsync. The header stays on juce_core alone, so
// anything that only needs the comparison does not pull in the event loop.
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace ovt {

namespace {
/// Splits a dotted version into its numbers. An empty result means the string
/// was not a version, which the comparison treats as "not newer" rather than
/// guessing.
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
    // A missing component is zero, so 1.1 and 1.1.0 are the same release and
    // 1.1 beats 1.0.9.
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

  // A feed that names a version but no page still tells us something, so the
  // releases page stands in rather than the whole answer being thrown away.
  if (info.url.isEmpty())
    info.url = "https://github.com/benjamindehli/overtonium/releases/latest";

  return info;
}

namespace {
/// Where the settings file is, rather than the file itself.
///
/// The options are what is kept, and each caller below builds a
/// PropertiesFile from them, uses it and lets it go. It was one shared object
/// in a function-local static, which read better and was a deadlock on unload
/// in any host that unloads the binary. The static is destroyed during static
/// destruction, meaning at unload rather than when the last instance is
/// removed, and PropertiesFile privately inherits juce::Timer, and a Timer
/// holds a reference to the one thread JUCE runs every timer on. Releasing the
/// last reference joins that thread with no timeout, and JUCE asserts on the
/// line above the join that the event system is still there, which by then it
/// is not. On Windows the join is under the loader lock that a thread also
/// needs in order to finish exiting, so it cannot complete. Reported against
/// 1.9.0 in FL Studio and Reaper, with MuLab unaffected because it keeps the
/// binary loaded.
///
/// Nothing about this is specific to the update check. The statics that remain
/// hold an array, a string list, a colour, an identifier and an image tile,
/// and what makes them safe is not that they are small: it is that none of
/// them owns a thread or needs the event system in order to be destroyed.
/// That, rather than the count, is the thing to check before adding one.
///
/// Writing the file by hand and dropping PropertiesFile would remove the
/// inheritance rather than work around it, but the options decide the path,
/// and a path that came out even slightly different would silently lose the
/// answer of everyone who has already been asked. Not worth it for a file
/// holding two booleans that is read a few times per window.
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

/// Reads one flag, with the file open for no longer than that.
bool flag(const char *key) {
  juce::PropertiesFile file(settingsOptions());
  return file.getBoolValue(key, false);
}

/// Writes one flag and puts it on disk before returning.
///
/// saveIfNeeded is called rather than left to the destructor, which would also
/// do it. The point of this file is that an answer survives the host, so the
/// write is where it can be seen to happen.
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

UpdateCheck::UpdateCheck() : juce::Thread("Overtonium update check") {}

UpdateCheck::~UpdateCheck() {
  // The wait has to outlast the request, and the arithmetic is what decides
  // the number rather than taste. The fetch allows five seconds of silence, so
  // a server that accepts and then says nothing holds the thread for five
  // seconds by design, and a wait shorter than that would expire while the
  // thread is still inside the network stack.
  //
  // Expiring is not a harmless timeout. stopThread kills the thread outright
  // at that point, and JUCE's own comment on that path warns of "locks and
  // events left in silly states": a thread killed holding an allocator or
  // socket lock takes the next thread that wants it with it. In a plugin that
  // is the message thread, which looks from the outside like the host hanging
  // while the audio carries on.
  //
  // Ten is never reached in practice. Anything destroying this has called
  // cancel() first, and the fetch checks that flag as it reads.
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

  // In pieces, for two reasons that both come from the far end being a web
  // server nobody here controls.
  //
  // It is bounded: the real feed is a couple of hundred bytes, and a plugin
  // should not hold whatever a server feels like sending. Reading it whole and
  // trimming afterwards would bound the string and not the memory.
  //
  // And it can be given up on. Each pass looks at the exit flag, so cancel()
  // takes effect within one chunk. A single long read would not look at it
  // until the far end had finished, which is a decision that belongs to the
  // server rather than to us.
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

  // Taken under the lock, since the message thread is free to replace it while
  // this runs. What it captures has to tolerate being called after whoever set
  // it has gone, because nothing joins this thread on the way out.
  std::function<void()> callback;

  {
    const juce::ScopedLock sl(lock);
    callback = listener;
  }

  if (callback != nullptr)
    juce::MessageManager::callAsync([callback] { callback(); });
}

} // namespace ovt
