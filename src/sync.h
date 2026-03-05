#pragma once

#include "interfaces.h"
#include <string>

// Glue between the Player and Firebase.
// Wires user-initiated playback events to Firebase writes,
// and Firebase SSE updates to player actions.
class Sync {
public:
  Sync(IPlayer &player, IFirebase &firebase, const std::string &roomCode,
       const std::string &userId);

  // Start listening for remote changes and hooking local events.
  void start();

  // Clean shutdown: leave the room, stop listeners.
  void stop();

  // Exposed for testing — simulate a remote update arriving.
  void handleRemoteUpdate(const PlaybackState &state);

  // Exposed for testing — simulate a user join/leave event.
  void handleUserEvent(const UserEvent &event);

private:
  IPlayer &player_;
  IFirebase &firebase_;
  std::string roomCode_;
  std::string userId_;

  // Called when the local user plays/pauses.
  void onLocalToggle(bool isPlaying, double positionSec);
};
