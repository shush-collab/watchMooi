#include "sync.h"
#include <chrono>
#include <cmath>
#include <iostream>

Sync::Sync(IPlayer &player, IFirebase &firebase, const std::string &roomCode,
           const std::string &userId)
    : player_(player), firebase_(firebase), roomCode_(roomCode),
      userId_(userId) {}

void Sync::start() {
  // Wire local events → Firebase
  player_.onPlaybackToggle(
      [this](bool isPlaying, double pos) { onLocalToggle(isPlaying, pos); });

  // Apply existing room state
  PlaybackState initial = firebase_.readPlaybackState(roomCode_);
  if (!initial.updatedBy.empty()) {
    std::cout << "[Sync] Applying existing room state: "
              << (initial.isPlaying ? "PLAYING" : "PAUSED") << " @ "
              << initial.positionSec << "s\n";
    player_.suppressNextEvent();
    player_.seekTo(initial.positionSec);
    initial.isPlaying ? player_.play() : player_.pause();
  }

  // Wire Firebase SSE → local player
  firebase_.listenForChanges(
      roomCode_, [this](const PlaybackState &s) { handleRemoteUpdate(s); });
  firebase_.listenForUserChanges(
      roomCode_, [this](const UserEvent &e) { handleUserEvent(e); });

  std::cout << "[Sync] Sync started for room " << roomCode_ << "\n";
}

void Sync::stop() {
  std::cout << "[Sync] Stopping sync...\n";
  firebase_.stopListening();
  firebase_.leaveRoom(roomCode_, userId_);
  std::cout << "[Sync] Sync stopped.\n";
}

void Sync::onLocalToggle(bool isPlaying, double positionSec) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  PlaybackState state{isPlaying, positionSec, userId_, now};

  std::cout << "[Sync] Local " << (isPlaying ? "PLAY" : "PAUSE") << " @ "
            << positionSec << "s → pushing to Firebase\n";
  firebase_.writePlaybackState(roomCode_, state);
}

void Sync::handleRemoteUpdate(const PlaybackState &state) {
  if (state.updatedBy == userId_)
    return;

  std::cout << "[Sync] Remote " << (state.isPlaying ? "PLAY" : "PAUSE") << " @ "
            << state.positionSec << "s from " << state.updatedBy << "\n";

  player_.suppressNextEvent();

  if (std::abs(player_.getPosition() - state.positionSec) > 1.0)
    player_.seekTo(state.positionSec);

  state.isPlaying ? player_.play() : player_.pause();
}

void Sync::handleUserEvent(const UserEvent &event) {
  if (event.userId == userId_)
    return;

  const std::string &label =
      event.displayName.empty() ? event.userId : event.displayName;

  if (event.joined) {
    std::cout << "\n🟢 " << label << " joined the room\n";
  } else {
    std::cout << "\n🔴 " << label << " left the room\n";
    if (player_.isPlaying()) {
      std::cout << "   ⏸  Pausing playback — " << label << " left.\n";
      player_.suppressNextEvent();
      player_.pause();
    }
  }
}
