#include "player.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

Player::Player() {
  mpv_ = mpv_create();
  if (!mpv_)
    throw std::runtime_error("Failed to create mpv context");

  // Enable default key bindings (space = toggle pause, q = quit, etc.)
  mpv_set_option_string(mpv_, "input-default-bindings", "yes");
  mpv_set_option_string(mpv_, "input-vo-keyboard", "yes");

  // Use a GUI video output
  mpv_set_option_string(mpv_, "vo", "gpu");

  // Window title
  mpv_set_option_string(mpv_, "title", "watchMooi");

  // Start paused
  int yes = 1;
  mpv_set_option(mpv_, "pause", MPV_FORMAT_FLAG, &yes);

  // Observe the "pause" property so we know when the user toggles it
  mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);

  if (mpv_initialize(mpv_) < 0)
    throw std::runtime_error("Failed to initialize mpv");
}

Player::~Player() {
  if (mpv_) {
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
  }
}

bool Player::loadFile(const std::string &path) {
  const char *cmd[] = {"loadfile", path.c_str(), nullptr};
  int err = mpv_command(mpv_, cmd);
  if (err < 0) {
    std::cerr << "[Player] loadfile error: " << mpv_error_string(err) << "\n";
    return false;
  }
  return true;
}

void Player::play() {
  int flag = 0;
  mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
}

void Player::pause() {
  int flag = 1;
  mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
}

void Player::seekTo(double positionSec) {
  std::string pos = std::to_string(positionSec);
  const char *cmd[] = {"seek", pos.c_str(), "absolute", nullptr};
  mpv_command(mpv_, cmd);
}

bool Player::isPlaying() const {
  int flag = 1; // default: paused
  mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
  return flag == 0;
}

double Player::getPosition() const {
  double pos = 0.0;
  mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &pos);
  return pos;
}

void Player::onPlaybackToggle(PlaybackCallback cb) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(cb);
}

void Player::suppressNextEvent() {
  std::lock_guard<std::mutex> lock(mutex_);
  suppressNext_ = true;
}

void Player::runLoop() {
  while (true) {
    mpv_event *event = mpv_wait_event(mpv_, -1); // block forever
    if (event->event_id == MPV_EVENT_SHUTDOWN)
      break;
    handleEvent(event);
  }
}

void Player::handleEvent(mpv_event *event) {
  if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
    auto *prop = static_cast<mpv_event_property *>(event->data);
    if (std::strcmp(prop->name, "pause") == 0 &&
        prop->format == MPV_FORMAT_FLAG) {
      int flag = *static_cast<int *>(prop->data);
      bool nowPlaying = (flag == 0);

      // Only fire if state actually changed
      if (nowPlaying == currentlyPlaying_)
        return;
      currentlyPlaying_ = nowPlaying;

      std::lock_guard<std::mutex> lock(mutex_);

      if (suppressNext_) {
        suppressNext_ = false;
        return;
      }

      if (callback_) {
        double pos = getPosition();
        callback_(nowPlaying, pos);
      }
    }
  }
}
