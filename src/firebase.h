#pragma once

#include "interfaces.h"

#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>

// JSON conversion helpers (implemented in firebase.cpp)
nlohmann::json playbackStateToJson(const PlaybackState &s);
PlaybackState playbackStateFromJson(const nlohmann::json &j);

// Firebase Realtime Database client using REST + SSE (Server-Sent Events).
class Firebase : public IFirebase {
public:
  using StateCallback = std::function<void(const PlaybackState &)>;
  using UserCallback = std::function<void(const UserEvent &)>;

  // dbUrl: e.g.
  // "https://watchmooi-default-rtdb.asia-southeast1.firebasedatabase.app"
  explicit Firebase(const std::string &dbUrl);
  ~Firebase();

  // Register this user in the room.
  bool joinRoom(const std::string &roomCode, const std::string &userId);

  // Remove this user from the room.
  bool leaveRoom(const std::string &roomCode, const std::string &userId);

  // Write playback state to the room.
  bool writePlaybackState(const std::string &roomCode,
                          const PlaybackState &state);

  // Read the current playback state (one-shot).
  PlaybackState readPlaybackState(const std::string &roomCode);

  // Start listening for playback state changes via SSE.
  // Runs in a background thread. Calls `cb` on every update.
  void listenForChanges(const std::string &roomCode, StateCallback cb);

  // Start listening for user join/leave events via SSE.
  void listenForUserChanges(const std::string &roomCode, UserCallback cb);

  // Stop all SSE listeners.
  void stopListening();

private:
  std::string dbUrl_;
  std::thread listenerThread_;
  std::thread userListenerThread_;
  std::atomic<bool> listening_{false};
  std::set<std::string> knownUsers_; // tracked for join/leave detection

  // Helpers
  std::string httpPut(const std::string &url, const std::string &body);
  std::string httpGet(const std::string &url);
  std::string httpDelete(const std::string &url);
};
