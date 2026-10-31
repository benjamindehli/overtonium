#pragma once

#include <array>
#include <atomic>

namespace ovt {

/// What the Adaptive temperament settled the held chord on, for the Tuning bar.
///
/// Published by the engine after every settle. Plain values only, so it can be
/// copied whole between threads. A key sliding to a new version reports the
/// version it is going to.
struct AdaptiveSnapshot {
  /// More than the engine can ever hold, so nothing is dropped.
  static constexpr int kMaxNotes = 32;

  struct Note {
    int key = 0;
    /// What it is tuned to, in Hz.
    double hz = 0.0;
    /// Which of its five versions, -2 to 2 commas from its place in just
    /// major on the root. Meaningless for the harmonic seventh.
    int version = 0;
    /// The minor seventh tuned 7:4 above its root, off the chain.
    bool harmonic = false;
  };

  /// Held keys, lowest first.
  std::array<Note, kMaxNotes> notes{};
  int count = 0;

  /// The key the harmonic seventh is tuned on, or -1 for none.
  int harmonicRoot = -1;

  /// The version each pitch class takes when struck into silence (the one it
  /// used last), indexed by semitones above the root.
  std::array<int, 12> remembered{};

  /// Where A sits, for reading each key against equal temperament.
  double referenceHz = 440.0;
};

/// Hands the latest value from one thread to another without either waiting.
///
/// Three copies: the writer's, the reader's and a middle one holding the
/// newest finished copy. Publishing and reading each swap with the middle in
/// one atomic exchange, so the reader always gets a whole copy and an unread
/// copy is simply replaced by a newer one.
///
/// One writer (the audio thread) and one reader.
template <typename T> class TripleBuffer {
public:
  /// From the writer only.
  void publish(const T &value) noexcept {
    copies[(size_t)back] = value;
    back = middle.exchange(back | kFresh, std::memory_order_acq_rel) & kIndex;
  }

  /// From the reader only. Copies the newest published value into @p out and
  /// returns true, or returns false if nothing has been published since the
  /// last read.
  bool read(T &out) noexcept {
    if ((middle.load(std::memory_order_acquire) & kFresh) == 0)
      return false;

    front = middle.exchange(front, std::memory_order_acq_rel) & kIndex;
    out = copies[(size_t)front];
    return true;
  }

private:
  static constexpr int kIndex = 3;
  static constexpr int kFresh = 4;

  std::array<T, 3> copies{};
  std::atomic<int> middle{1};
  int back = 0;  // the writer's
  int front = 2; // the reader's
};

} // namespace ovt
