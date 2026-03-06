#pragma once

#include <cstdint>
#include <functional>
#include <string>

// ── PlaybackState ───────────────────────────────────────────────────────────

struct PlaybackState {
  bool isPlaying = false;
  double positionSec = 0.0;
  std::string updatedBy;
  int64_t updatedAt = 0; // unix ms
};

// ── Video metadata (for mismatch detection) ─────────────────────────────────

struct VideoMeta {
  std::string filename;     // basename of the video file
  double durationSec = 0.0; // total length in seconds
};

// ── User presence event ─────────────────────────────────────────────────────

struct UserEvent {
  std::string userId;
  std::string displayName; // human-readable name (may be empty)
  bool joined;             // true = joined, false = left
};

// ── Abstract interfaces for testability ─────────────────────────────────────

class IPlayer {
public:
  using PlaybackCallback =
      std::function<void(bool isPlaying, double positionSec)>;

  virtual ~IPlayer() = default;

  virtual bool loadFile(const std::string &path) = 0;
  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void seekTo(double positionSec) = 0;
  virtual bool isPlaying() const = 0;
  virtual double getPosition() const = 0;
  virtual double getDuration() const = 0;
  virtual void onPlaybackToggle(PlaybackCallback cb) = 0;
  virtual void suppressNextEvent() = 0;
};

class IFirebase {
public:
  using StateCallback = std::function<void(const PlaybackState &)>;
  using UserCallback = std::function<void(const UserEvent &)>;

  virtual ~IFirebase() = default;

  virtual bool joinRoom(const std::string &roomCode, const std::string &userId,
                        const std::string &displayName = "") = 0;
  virtual bool leaveRoom(const std::string &roomCode,
                         const std::string &userId) = 0;
  virtual bool writePlaybackState(const std::string &roomCode,
                                  const PlaybackState &state) = 0;
  virtual PlaybackState readPlaybackState(const std::string &roomCode) = 0;
  virtual void listenForChanges(const std::string &roomCode,
                                StateCallback cb) = 0;
  virtual void listenForUserChanges(const std::string &roomCode,
                                    UserCallback cb) = 0;
  virtual bool writeVideoMeta(const std::string &roomCode,
                              const VideoMeta &meta) = 0;
  virtual VideoMeta readVideoMeta(const std::string &roomCode) = 0;
  virtual void stopListening() = 0;
};
