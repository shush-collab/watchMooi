#pragma once

#include "interfaces.h"
#include <mpv/client.h>
#include <mutex>
#include <string>

// Thin wrapper around libmpv for video playback.
class Player : public IPlayer {
public:
  Player();
  ~Player();

  bool loadFile(const std::string &path) override;
  void play() override;
  void pause() override;
  void seekTo(double positionSec) override;
  bool isPlaying() const override;
  double getPosition() const override;
  void onPlaybackToggle(PlaybackCallback cb) override;
  void suppressNextEvent() override;

  // Run the mpv event loop. Blocks until the window is closed.
  void runLoop();

private:
  mpv_handle *mpv_ = nullptr;
  PlaybackCallback callback_;
  mutable std::mutex mutex_;
  bool suppressNext_ = false;
  bool currentlyPlaying_ = false;

  void handleEvent(mpv_event *event);
};
