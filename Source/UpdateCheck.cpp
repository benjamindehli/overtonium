#include "UpdateCheck.h"

// For MessageManager::callAsync. The header stays on juce_core alone, so
// anything that only needs the comparison does not pull in the event loop.
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

UpdateCheck::UpdateCheck() : juce::Thread("Overtonium update check") {}

UpdateCheck::~UpdateCheck() {
  // A short wait. The request has its own timeout well inside this, and an
  // editor closing should not sit on a socket that is not answering.
  stopThread(3000);
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

  // Bounded, because the far end is a web server and a plugin should not read
  // whatever it feels like sending. The real file is a couple of hundred
  // bytes.
  const auto body = stream->readString().substring(0, 4096);

  if (threadShouldExit())
    return;

  const auto info = parseReleaseJson(body);

  if (!info.has_value() || !isNewerVersion(info->version, current))
    return;

  {
    const juce::ScopedLock sl(lock);
    found = info;
  }

  // The editor owns what happens next, and it lives on the message thread.
  if (onResult != nullptr) {
    auto callback = onResult;
    juce::MessageManager::callAsync([callback] { callback(); });
  }
}

} // namespace ovt
