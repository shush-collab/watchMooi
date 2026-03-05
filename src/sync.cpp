#include "sync.h"
#include <chrono>
#include <cmath>
#include <iostream>

Sync::Sync(IPlayer &player, IFirebase &firebase, const std::string &roomCode,
           const std::string &userId)
    : player_(player), firebase_(firebase), roomCode_(roomCode),
      userId_(userId) {}

void Sync::start() {
  // Wire up local events → Firebase
  player_.onPlaybackToggle([this](bool isPlaying, double positionSec) {
    onLocalToggle(isPlaying, positionSec);
  });

  // Read initial state from Firebase and apply it
  PlaybackState initial = firebase_.readPlaybackState(roomCode_);
  if (!initial.updatedBy.empty()) {
    std::cout << "[Sync] Applying existing room state: "
              << (initial.isPlaying ? "PLAYING" : "PAUSED") << " @ "
              << initial.positionSec << "s\n";

    player_.suppressNextEvent();
    player_.seekTo(initial.positionSec);
    if (initial.isPlaying)
      player_.play();
    else
      player_.pause();
  }

  // Wire up Firebase SSE → local player
  firebase_.listenForChanges(roomCode_, [this](const PlaybackState &state) {
    handleRemoteUpdate(state);
  });

  // Wire up user presence events
  firebase_.listenForUserChanges(
      roomCode_, [this](const UserEvent &event) { handleUserEvent(event); });

  std::cout << "[Sync] Sync started for room " << roomCode_ << "\n";
}

void Sync::stop() {
  std::cout << "[Sync] Stopping sync...\n";

  // Stop SSE listeners first so no more events arrive
  firebase_.stopListening();

  // Remove ourselves from the room
  firebase_.leaveRoom(roomCode_, userId_);

  std::cout << "[Sync] Sync stopped.\n";
}

void Sync::onLocalToggle(bool isPlaying, double positionSec) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  PlaybackState state;
  state.isPlaying = isPlaying;
  state.positionSec = positionSec;
  state.updatedBy = userId_;
  state.updatedAt = now;

  std::cout << "[Sync] Local " << (isPlaying ? "PLAY" : "PAUSE") << " @ "
            << positionSec << "s → pushing to Firebase\n";

  firebase_.writePlaybackState(roomCode_, state);
}

void Sync::handleRemoteUpdate(const PlaybackState &state) {
  // Ignore our own updates (echo suppression)
  if (state.updatedBy == userId_)
    return;

  std::cout << "[Sync] Remote " << (state.isPlaying ? "PLAY" : "PAUSE") << " @ "
            << state.positionSec << "s from " << state.updatedBy << "\n";

  // Suppress the event that our player will fire when we change its state
  player_.suppressNextEvent();

  // Sync position if it differs by more than 1 second
  double currentPos = player_.getPosition();
  if (std::abs(currentPos - state.positionSec) > 1.0) {
    player_.seekTo(state.positionSec);
  }

  if (state.isPlaying)
    player_.play();
  else
    player_.pause();
}

void Sync::handleUserEvent(const UserEvent &event) {
  if (event.userId == userId_)
    return; // ignore our own events

  std::string label =
      event.displayName.empty() ? event.userId : event.displayName;

  if (event.joined) {
    std::cout << "\n🟢 " << label << " joined the room\n";
  } else {
    std::cout << "\n🔴 " << label << " left the room\n";

    // Pause playback when the other user leaves
    if (player_.isPlaying()) {
      std::cout << "   ⏸  Pausing playback — " << label << " left.\n";
      player_.suppressNextEvent();
      player_.pause();
    }
  }
}
