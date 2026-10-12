#pragma once

#include <array>
#include <atomic>

namespace ovt {

/// What the Adaptive temperament settled the held chord on, for the screen.
///
/// Published by the engine after every settle, on every key down and up and
/// on pedal release, and read by the Tuning bar. Plain values only, so it can
/// be copied whole between threads. It says where each key is going rather
/// than where it is: a key sliding to a new version reports the version.
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

  /// The version each pitch class takes when struck into silence, the one it
  /// used last, indexed by semitones above the root. Says where a key that is
  /// not held would land.
  std::array<int, 12> remembered{};

  /// Where A sits, for reading each key against equal temperament.
  double referenceHz = 440.0;
};

/// Hands the latest of something from one thread to another without either
/// waiting for the other.
///
/// Three copies: one the writer fills, one the reader reads, and one between
/// them holding the newest finished copy. Publishing swaps the filled copy
/// into the middle and reading swaps the middle out, each with one atomic
/// exchange, so the writer never waits on the reader and the reader always
/// gets a whole copy, never one half written. A copy published before the
/// reader got to the last one replaces it, which is the point: only the
/// newest matters.
///
/// One writer and one reader. The writer here is the audio thread.
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
