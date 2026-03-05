#pragma once

#include "interfaces.h"
#include <functional>
#include <mpv/client.h>
#include <mutex>
#include <string>

// Thin wrapper around libmpv for video playback.
// Provides play/pause/seek and a callback when the user toggles playback.
class Player : public IPlayer {
public:
  // Called when the USER (not a remote sync) toggles play/pause.
  // (isPlaying, positionSec)
  using PlaybackCallback =
      std::function<void(bool isPlaying, double positionSec)>;

  Player();
  ~Player();

  // Load and display a video file. Starts paused.
  bool loadFile(const std::string &path);

  // Playback controls
  void play();
  void pause();
  void seekTo(double positionSec);

  // Queries
  bool isPlaying() const;
  double getPosition() const;

  // Register callback for user-initiated play/pause.
  void onPlaybackToggle(PlaybackCallback cb);

  // Run the mpv event loop. Blocks until the window is closed.
  void runLoop();

  // Suppress the next play/pause event from firing the callback.
  // Used by Sync to avoid echo when applying remote state.
  void suppressNextEvent();

private:
  mpv_handle *mpv_ = nullptr;
  PlaybackCallback callback_;
  mutable std::mutex mutex_;
  bool suppressNext_ = false;
  bool currentlyPlaying_ = false;

  void handleEvent(mpv_event *event);
};
