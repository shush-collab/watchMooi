#pragma once

#include "interfaces.h"

#include <atomic>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

// JSON conversion helpers (implemented in firebase.cpp)
nlohmann::json playbackStateToJson(const PlaybackState &s);
PlaybackState playbackStateFromJson(const nlohmann::json &j);

// Firebase Realtime Database client using REST + SSE.
class Firebase : public IFirebase {
public:
  explicit Firebase(const std::string &dbUrl);
  ~Firebase();

  bool joinRoom(const std::string &roomCode, const std::string &userId,
                const std::string &displayName = "") override;
  bool leaveRoom(const std::string &roomCode,
                 const std::string &userId) override;
  bool writePlaybackState(const std::string &roomCode,
                          const PlaybackState &state) override;
  PlaybackState readPlaybackState(const std::string &roomCode) override;
  void listenForChanges(const std::string &roomCode, StateCallback cb) override;
  void listenForUserChanges(const std::string &roomCode,
                            UserCallback cb) override;
  bool writeVideoMeta(const std::string &roomCode,
                      const VideoMeta &meta) override;
  VideoMeta readVideoMeta(const std::string &roomCode) override;
  void stopListening() override;

private:
  std::string dbUrl_;
  std::thread listenerThread_;
  std::thread userListenerThread_;
  std::atomic<bool> listening_{false};
  std::map<std::string, std::string> knownUsers_; // userId -> displayName

  std::string httpRequest(const std::string &url, const std::string &method,
                          const std::string &body = "");
};
