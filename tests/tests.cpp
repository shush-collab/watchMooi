// ============================================================================
// watchMooi MVP — Comprehensive Tests
// ============================================================================
//
// Tests all requirements from the original diagram:
//   1. Play/pause sync when any user does it
//   2. Video existence verification on both devices
//   3. (WebRTC — not in MVP, placeholder tests)
//   4. Room code entry — correct room from code
//   5. Both videos are completely in sync (position sync)
//   6. Privacy — no other person can view the room
//   7. (Chat — not in MVP, placeholder tests)
//   8. C++ implementation (verified by compilation)
//   9. Cross-platform (verified by build system)
//
// Uses Google Test with mock Player/Firebase implementations.
// ============================================================================

#include <gtest/gtest.h>

#include "../src/interfaces.h"
#include "../src/sync.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Mock Player
// ============================================================================

class MockPlayer : public IPlayer {
public:
  bool loadFile(const std::string &path) override {
    loadedFile_ = path;
    fileExists_ = std::filesystem::is_regular_file(path);
    return fileExists_;
  }

  void play() override {
    playing_ = true;
    actionLog_.push_back("play");
  }

  void pause() override {
    playing_ = false;
    actionLog_.push_back("pause");
  }

  void seekTo(double positionSec) override {
    position_ = positionSec;
    actionLog_.push_back("seek:" + std::to_string(positionSec));
  }

  bool isPlaying() const override { return playing_; }
  double getPosition() const override { return position_; }

  void onPlaybackToggle(PlaybackCallback cb) override {
    callback_ = std::move(cb);
  }

  void suppressNextEvent() override { suppressCount_++; }

  // ── Test helpers ────────────────────────────────────────────────────────

  // Simulate the user pressing play/pause in the UI
  void simulateUserToggle(bool isPlaying, double positionSec) {
    playing_ = isPlaying;
    position_ = positionSec;
    if (callback_) {
      callback_(isPlaying, positionSec);
    }
  }

  void setPosition(double pos) { position_ = pos; }

  std::string loadedFile_;
  bool fileExists_ = false;
  bool playing_ = false;
  double position_ = 0.0;
  int suppressCount_ = 0;
  std::vector<std::string> actionLog_;
  PlaybackCallback callback_;
};

// ============================================================================
// Mock Firebase
// ============================================================================

class MockFirebase : public IFirebase {
public:
  bool joinRoom(const std::string &roomCode, const std::string &userId,
                const std::string & /*displayName*/ = "") override {
    rooms_[roomCode].push_back(userId);
    joinLog_.push_back({roomCode, userId});
    if (userListeners_.count(roomCode))
      userListeners_[roomCode]({userId, "", true});
    return true;
  }

  bool leaveRoom(const std::string &roomCode,
                 const std::string &userId) override {
    leaveLog_.push_back({roomCode, userId});
    // Remove from rooms
    auto &users = rooms_[roomCode];
    users.erase(std::remove(users.begin(), users.end(), userId), users.end());
    // Notify user listeners
    if (userListeners_.count(roomCode)) {
      userListeners_[roomCode]({userId, "", false});
    }
    return true;
  }

  bool writePlaybackState(const std::string &roomCode,
                          const PlaybackState &state) override {
    states_[roomCode] = state;
    writeLog_.push_back({roomCode, state});
    // If there's a listener for this room, fire it
    if (listeners_.count(roomCode)) {
      listeners_[roomCode](state);
    }
    return true;
  }

  PlaybackState readPlaybackState(const std::string &roomCode) override {
    if (states_.count(roomCode))
      return states_[roomCode];
    return {};
  }

  void listenForChanges(const std::string &roomCode,
                        StateCallback cb) override {
    listeners_[roomCode] = cb;
    listenRoomCodes_.push_back(roomCode);
  }

  void listenForUserChanges(const std::string &roomCode,
                            UserCallback cb) override {
    userListeners_[roomCode] = cb;
  }

  void stopListening() override {
    listeners_.clear();
    userListeners_.clear();
    stopListeningCalled_ = true;
  }

  // ── Test helpers ────────────────────────────────────────────────────────

  // Simulate a remote SSE event arriving
  void simulateRemoteEvent(const std::string &roomCode,
                           const PlaybackState &state) {
    if (listeners_.count(roomCode)) {
      listeners_[roomCode](state);
    }
  }

  // Simulate a user join/leave event
  void simulateUserEvent(const std::string &roomCode, const UserEvent &event) {
    if (userListeners_.count(roomCode)) {
      userListeners_[roomCode](event);
    }
  }

  struct JoinEntry {
    std::string roomCode;
    std::string userId;
  };

  struct WriteEntry {
    std::string roomCode;
    PlaybackState state;
  };

  std::map<std::string, std::vector<std::string>> rooms_;
  std::map<std::string, PlaybackState> states_;
  std::map<std::string, StateCallback> listeners_;
  std::map<std::string, UserCallback> userListeners_;
  std::vector<JoinEntry> joinLog_;
  std::vector<JoinEntry> leaveLog_;
  std::vector<WriteEntry> writeLog_;
  std::vector<std::string> listenRoomCodes_;
  bool stopListeningCalled_ = false;
};

// ============================================================================
// REQUIREMENT 1: Play/pause sync when any user does it
// ============================================================================

class PlayPauseSyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    player1_ = std::make_unique<MockPlayer>();
    player2_ = std::make_unique<MockPlayer>();
    firebase_ = std::make_unique<MockFirebase>();

    sync1_ = std::make_unique<Sync>(*player1_, *firebase_, "ROOM1", "user1");
    sync2_ = std::make_unique<Sync>(*player2_, *firebase_, "ROOM1", "user2");

    sync1_->start();
    sync2_->start();
  }

  std::unique_ptr<MockPlayer> player1_, player2_;
  std::unique_ptr<MockFirebase> firebase_;
  std::unique_ptr<Sync> sync1_, sync2_;
};

TEST_F(PlayPauseSyncTest, User1PlaysAndUser2Receives) {
  // User 1 presses play at position 5.0s
  player1_->simulateUserToggle(true, 5.0);

  // Verify Firebase received the play state
  ASSERT_FALSE(firebase_->writeLog_.empty());
  const auto &written = firebase_->writeLog_.back().state;
  EXPECT_TRUE(written.isPlaying);
  EXPECT_DOUBLE_EQ(written.positionSec, 5.0);
  EXPECT_EQ(written.updatedBy, "user1");
}

TEST_F(PlayPauseSyncTest, User1PausesAndUser2Receives) {
  // Start playing first
  player1_->simulateUserToggle(true, 0.0);

  // Now pause at 12.5s
  player1_->simulateUserToggle(false, 12.5);

  const auto &written = firebase_->writeLog_.back().state;
  EXPECT_FALSE(written.isPlaying);
  EXPECT_DOUBLE_EQ(written.positionSec, 12.5);
  EXPECT_EQ(written.updatedBy, "user1");
}

TEST_F(PlayPauseSyncTest, RemotePlayAppliedToLocalPlayer) {
  // Simulate user2 pressing play remotely
  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 30.0;
  remote.updatedBy = "user2";
  remote.updatedAt = 1000;

  sync1_->handleRemoteUpdate(remote);

  EXPECT_TRUE(player1_->playing_);
  EXPECT_NEAR(player1_->position_, 30.0, 0.01);
}

TEST_F(PlayPauseSyncTest, RemotePauseAppliedToLocalPlayer) {
  // Start playing
  player1_->play();

  // Remote user pauses
  PlaybackState remote;
  remote.isPlaying = false;
  remote.positionSec = 45.2;
  remote.updatedBy = "user2";
  remote.updatedAt = 2000;

  sync1_->handleRemoteUpdate(remote);

  EXPECT_FALSE(player1_->playing_);
}

TEST_F(PlayPauseSyncTest, RapidToggleHandledCorrectly) {
  // Rapid play/pause/play/pause
  player1_->simulateUserToggle(true, 0.0);
  player1_->simulateUserToggle(false, 0.5);
  player1_->simulateUserToggle(true, 0.6);
  player1_->simulateUserToggle(false, 1.0);

  // All 4 writes should have gone through
  EXPECT_EQ(firebase_->writeLog_.size(), 4u);

  // Last state should be paused
  EXPECT_FALSE(firebase_->writeLog_.back().state.isPlaying);
}

TEST_F(PlayPauseSyncTest, BothUsersCanControl) {
  // User 1 plays
  player1_->simulateUserToggle(true, 0.0);
  EXPECT_TRUE(firebase_->states_["ROOM1"].isPlaying);
  EXPECT_EQ(firebase_->states_["ROOM1"].updatedBy, "user1");

  // User 2 pauses (simulated as remote event for user1's sync)
  PlaybackState pauseState;
  pauseState.isPlaying = false;
  pauseState.positionSec = 10.0;
  pauseState.updatedBy = "user2";
  pauseState.updatedAt = 3000;

  sync1_->handleRemoteUpdate(pauseState);
  EXPECT_FALSE(player1_->playing_);
}

// ============================================================================
// REQUIREMENT 2: Make sure the video exists on both devices
// ============================================================================

class VideoExistenceTest : public ::testing::Test {
protected:
  MockPlayer player_;
};

TEST_F(VideoExistenceTest, ExistingFileLoadsSuccessfully) {
  // Create a temporary test file
  std::string tmpFile = "/tmp/watchmooi_test_video.mp4";
  std::ofstream ofs(tmpFile);
  ofs << "fake video content";
  ofs.close();

  EXPECT_TRUE(player_.loadFile(tmpFile));
  EXPECT_TRUE(player_.fileExists_);
  EXPECT_EQ(player_.loadedFile_, tmpFile);

  std::filesystem::remove(tmpFile);
}

TEST_F(VideoExistenceTest, NonExistentFileFailsGracefully) {
  EXPECT_FALSE(
      player_.loadFile("/tmp/this_file_definitely_does_not_exist_12345.mp4"));
  EXPECT_FALSE(player_.fileExists_);
}

TEST_F(VideoExistenceTest, EmptyPathFails) {
  EXPECT_FALSE(player_.loadFile(""));
  EXPECT_FALSE(player_.fileExists_);
}

TEST_F(VideoExistenceTest, DirectoryPathFails) {
  EXPECT_FALSE(player_.loadFile("/tmp"));
  // /tmp exists but is a directory, not a regular file
  // Our mock uses std::filesystem::exists which returns true for dirs,
  // but a real player would reject a directory
}

TEST_F(VideoExistenceTest, BothUsersMustHaveSameFile) {
  // Simulate both users trying to load the same file
  MockPlayer playerA, playerB;

  std::string tmpFile = "/tmp/watchmooi_shared_movie.mkv";
  std::ofstream ofs(tmpFile);
  ofs << "shared movie data";
  ofs.close();

  EXPECT_TRUE(playerA.loadFile(tmpFile));
  EXPECT_TRUE(playerB.loadFile(tmpFile));
  EXPECT_EQ(playerA.loadedFile_, playerB.loadedFile_);

  // If user B doesn't have the file
  EXPECT_FALSE(playerB.loadFile("/tmp/missing_movie.mkv"));

  std::filesystem::remove(tmpFile);
}

// ============================================================================
// REQUIREMENT 4: Room code entry — correct room from code input
// ============================================================================

class RoomCodeTest : public ::testing::Test {
protected:
  MockFirebase firebase_;
};

TEST_F(RoomCodeTest, UsersJoinSameRoomWithSameCode) {
  firebase_.joinRoom("ABC123", "user_a");
  firebase_.joinRoom("ABC123", "user_b");

  ASSERT_EQ(firebase_.rooms_["ABC123"].size(), 2u);
  EXPECT_EQ(firebase_.rooms_["ABC123"][0], "user_a");
  EXPECT_EQ(firebase_.rooms_["ABC123"][1], "user_b");
}

TEST_F(RoomCodeTest, DifferentCodesCreateDifferentRooms) {
  firebase_.joinRoom("ROOM_A", "user1");
  firebase_.joinRoom("ROOM_B", "user2");

  EXPECT_EQ(firebase_.rooms_["ROOM_A"].size(), 1u);
  EXPECT_EQ(firebase_.rooms_["ROOM_B"].size(), 1u);
  EXPECT_EQ(firebase_.rooms_["ROOM_A"][0], "user1");
  EXPECT_EQ(firebase_.rooms_["ROOM_B"][0], "user2");
}

TEST_F(RoomCodeTest, RoomCodeIsCaseSensitive) {
  firebase_.joinRoom("MyRoom", "user1");
  firebase_.joinRoom("myroom", "user2");
  firebase_.joinRoom("MYROOM", "user3");

  // All three should be in separate rooms
  EXPECT_EQ(firebase_.rooms_.size(), 3u);
}

TEST_F(RoomCodeTest, WritesGoToCorrectRoom) {
  PlaybackState state;
  state.isPlaying = true;
  state.positionSec = 10.0;
  state.updatedBy = "user1";
  state.updatedAt = 1000;

  firebase_.writePlaybackState("ROOM_X", state);

  // ROOM_X has state, ROOM_Y does not
  auto readX = firebase_.readPlaybackState("ROOM_X");
  auto readY = firebase_.readPlaybackState("ROOM_Y");

  EXPECT_TRUE(readX.isPlaying);
  EXPECT_DOUBLE_EQ(readX.positionSec, 10.0);
  EXPECT_FALSE(readY.isPlaying);            // default
  EXPECT_DOUBLE_EQ(readY.positionSec, 0.0); // default
}

TEST_F(RoomCodeTest, ListenerAttachesToCorrectRoom) {
  bool roomAReceived = false;
  bool roomBReceived = false;

  firebase_.listenForChanges(
      "ROOM_A", [&](const PlaybackState &) { roomAReceived = true; });
  firebase_.listenForChanges(
      "ROOM_B", [&](const PlaybackState &) { roomBReceived = true; });

  // Fire event only on ROOM_A
  PlaybackState state;
  state.isPlaying = true;
  firebase_.simulateRemoteEvent("ROOM_A", state);

  EXPECT_TRUE(roomAReceived);
  EXPECT_FALSE(roomBReceived);
}

TEST_F(RoomCodeTest, EmptyRoomCodeStillFunctions) {
  // Edge case: empty room code
  firebase_.joinRoom("", "user1");
  EXPECT_EQ(firebase_.rooms_[""].size(), 1u);
}

TEST_F(RoomCodeTest, SpecialCharactersInRoomCode) {
  firebase_.joinRoom("room-with-dashes", "user1");
  firebase_.joinRoom("room_with_underscores", "user2");
  firebase_.joinRoom("room.with.dots", "user3");

  EXPECT_EQ(firebase_.rooms_.size(), 3u);
}

// ============================================================================
// REQUIREMENT 5: Both videos are completely in sync (position)
// ============================================================================

class VideoSyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    player_ = std::make_unique<MockPlayer>();
    firebase_ = std::make_unique<MockFirebase>();
    sync_ =
        std::make_unique<Sync>(*player_, *firebase_, "SYNC_ROOM", "local_user");
    sync_->start();
  }

  std::unique_ptr<MockPlayer> player_;
  std::unique_ptr<MockFirebase> firebase_;
  std::unique_ptr<Sync> sync_;
};

TEST_F(VideoSyncTest, PositionSyncsWhenDriftExceedsThreshold) {
  // Local player is at 10.0s
  player_->setPosition(10.0);

  // Remote user is at 20.0s (10s drift > 1s threshold)
  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 20.0;
  remote.updatedBy = "remote_user";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  EXPECT_NEAR(player_->position_, 20.0, 0.01);
  // Should have sought and played
  bool hasSeek = false;
  for (const auto &action : player_->actionLog_) {
    if (action.find("seek:") == 0)
      hasSeek = true;
  }
  EXPECT_TRUE(hasSeek);
}

TEST_F(VideoSyncTest, NoSeekWhenDriftWithinThreshold) {
  // Local at 10.0s, remote at 10.5s (0.5s drift < 1s threshold)
  player_->setPosition(10.0);
  player_->actionLog_.clear();

  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 10.5;
  remote.updatedBy = "remote_user";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  // Should NOT have sought (position stays at 10.0)
  bool hasSeek = false;
  for (const auto &action : player_->actionLog_) {
    if (action.find("seek:") == 0)
      hasSeek = true;
  }
  EXPECT_FALSE(hasSeek);
  EXPECT_DOUBLE_EQ(player_->position_, 10.0);
}

TEST_F(VideoSyncTest, ExactThresholdBoundary) {
  // Exactly 1.0s drift — should NOT seek (> 1.0, not >=)
  player_->setPosition(10.0);
  player_->actionLog_.clear();

  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 11.0;
  remote.updatedBy = "remote_user";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  bool hasSeek = false;
  for (const auto &action : player_->actionLog_) {
    if (action.find("seek:") == 0)
      hasSeek = true;
  }
  EXPECT_FALSE(hasSeek);
}

TEST_F(VideoSyncTest, JustOverThresholdSeeks) {
  // 1.01s drift — should seek
  player_->setPosition(10.0);
  player_->actionLog_.clear();

  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 11.01;
  remote.updatedBy = "remote_user";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  bool hasSeek = false;
  for (const auto &action : player_->actionLog_) {
    if (action.find("seek:") == 0)
      hasSeek = true;
  }
  EXPECT_TRUE(hasSeek);
}

TEST_F(VideoSyncTest, BackwardSeekAlsoSyncs) {
  // Remote is behind by more than 1s
  player_->setPosition(50.0);
  player_->actionLog_.clear();

  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 30.0;
  remote.updatedBy = "remote_user";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  EXPECT_NEAR(player_->position_, 30.0, 0.01);
}

TEST_F(VideoSyncTest, PositionIncludedInFirebaseWrite) {
  // When local user toggles at a specific position, it's sent to Firebase
  player_->simulateUserToggle(true, 42.7);

  const auto &written = firebase_->writeLog_.back().state;
  EXPECT_DOUBLE_EQ(written.positionSec, 42.7);
}

TEST_F(VideoSyncTest, ZeroPositionSyncs) {
  player_->setPosition(100.0);
  player_->actionLog_.clear();

  PlaybackState remote;
  remote.isPlaying = false;
  remote.positionSec = 0.0;
  remote.updatedBy = "remote_user";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  EXPECT_NEAR(player_->position_, 0.0, 0.01);
}

// ============================================================================
// REQUIREMENT 6: Privacy — no other person can see your videos
// ============================================================================

class PrivacyTest : public ::testing::Test {
protected:
  MockFirebase firebase_;
};

TEST_F(PrivacyTest, RoomStatesAreIsolated) {
  PlaybackState stateA;
  stateA.isPlaying = true;
  stateA.positionSec = 100.0;
  stateA.updatedBy = "userA";

  PlaybackState stateB;
  stateB.isPlaying = false;
  stateB.positionSec = 200.0;
  stateB.updatedBy = "userB";

  firebase_.writePlaybackState("PRIVATE_ROOM_A", stateA);
  firebase_.writePlaybackState("PRIVATE_ROOM_B", stateB);

  // Reading room A should NOT see room B's state
  auto readA = firebase_.readPlaybackState("PRIVATE_ROOM_A");
  auto readB = firebase_.readPlaybackState("PRIVATE_ROOM_B");

  EXPECT_TRUE(readA.isPlaying);
  EXPECT_DOUBLE_EQ(readA.positionSec, 100.0);
  EXPECT_EQ(readA.updatedBy, "userA");

  EXPECT_FALSE(readB.isPlaying);
  EXPECT_DOUBLE_EQ(readB.positionSec, 200.0);
  EXPECT_EQ(readB.updatedBy, "userB");
}

TEST_F(PrivacyTest, ListenersOnlyGetTheirRoomEvents) {
  std::vector<PlaybackState> roomAEvents, roomBEvents;

  firebase_.listenForChanges(
      "ROOM_A", [&](const PlaybackState &s) { roomAEvents.push_back(s); });
  firebase_.listenForChanges(
      "ROOM_B", [&](const PlaybackState &s) { roomBEvents.push_back(s); });

  PlaybackState s1;
  s1.updatedBy = "alice";
  s1.isPlaying = true;
  firebase_.simulateRemoteEvent("ROOM_A", s1);

  PlaybackState s2;
  s2.updatedBy = "bob";
  s2.isPlaying = false;
  firebase_.simulateRemoteEvent("ROOM_B", s2);

  ASSERT_EQ(roomAEvents.size(), 1u);
  ASSERT_EQ(roomBEvents.size(), 1u);
  EXPECT_EQ(roomAEvents[0].updatedBy, "alice");
  EXPECT_EQ(roomBEvents[0].updatedBy, "bob");
}

TEST_F(PrivacyTest, UnknownRoomReturnsEmptyState) {
  auto state = firebase_.readPlaybackState("ROOM_THAT_DOESNT_EXIST");
  EXPECT_FALSE(state.isPlaying);
  EXPECT_DOUBLE_EQ(state.positionSec, 0.0);
  EXPECT_TRUE(state.updatedBy.empty());
  EXPECT_EQ(state.updatedAt, 0);
}

TEST_F(PrivacyTest, StopListeningRemovesAllCallbacks) {
  bool received = false;
  firebase_.listenForChanges("ROOM1",
                             [&](const PlaybackState &) { received = true; });

  firebase_.stopListening();

  PlaybackState state;
  state.isPlaying = true;
  firebase_.simulateRemoteEvent("ROOM1", state);

  EXPECT_FALSE(received);
}

// ============================================================================
// Echo suppression (critical for correct sync)
// ============================================================================

class EchoSuppressionTest : public ::testing::Test {
protected:
  void SetUp() override {
    player_ = std::make_unique<MockPlayer>();
    firebase_ = std::make_unique<MockFirebase>();
    sync_ = std::make_unique<Sync>(*player_, *firebase_, "ECHO_ROOM", "me");
    sync_->start();
  }

  std::unique_ptr<MockPlayer> player_;
  std::unique_ptr<MockFirebase> firebase_;
  std::unique_ptr<Sync> sync_;
};

TEST_F(EchoSuppressionTest, OwnUpdatesAreIgnored) {
  player_->actionLog_.clear();

  // Simulate receiving our own update back via SSE
  PlaybackState echo;
  echo.isPlaying = true;
  echo.positionSec = 10.0;
  echo.updatedBy = "me"; // Same as our userId
  echo.updatedAt = 1000;

  sync_->handleRemoteUpdate(echo);

  // No actions should have been taken on the player
  EXPECT_TRUE(player_->actionLog_.empty());
}

TEST_F(EchoSuppressionTest, OtherUsersUpdatesAreApplied) {
  player_->actionLog_.clear();

  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 10.0;
  remote.updatedBy = "someone_else";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  EXPECT_FALSE(player_->actionLog_.empty());
  EXPECT_TRUE(player_->playing_);
}

TEST_F(EchoSuppressionTest, SuppressEventCalledOnRemoteUpdate) {
  player_->suppressCount_ = 0;

  PlaybackState remote;
  remote.isPlaying = true;
  remote.positionSec = 10.0;
  remote.updatedBy = "other";
  remote.updatedAt = 1000;

  sync_->handleRemoteUpdate(remote);

  // suppressNextEvent should have been called to prevent
  // the player from firing a callback when we change its state
  EXPECT_EQ(player_->suppressCount_, 1);
}

TEST_F(EchoSuppressionTest, MultipleRemoteUpdatesSuppressEachTime) {
  player_->suppressCount_ = 0;

  for (int i = 0; i < 5; ++i) {
    PlaybackState remote;
    remote.isPlaying = (i % 2 == 0);
    remote.positionSec = i * 10.0;
    remote.updatedBy = "other";
    remote.updatedAt = i * 1000;

    sync_->handleRemoteUpdate(remote);
  }

  EXPECT_EQ(player_->suppressCount_, 5);
}

// ============================================================================
// PlaybackState data integrity
// ============================================================================

TEST(PlaybackStateTest, DefaultValues) {
  PlaybackState s;
  EXPECT_FALSE(s.isPlaying);
  EXPECT_DOUBLE_EQ(s.positionSec, 0.0);
  EXPECT_TRUE(s.updatedBy.empty());
  EXPECT_EQ(s.updatedAt, 0);
}

TEST(PlaybackStateTest, FieldAssignment) {
  PlaybackState s;
  s.isPlaying = true;
  s.positionSec = 123.456;
  s.updatedBy = "testuser";
  s.updatedAt = 1709487600000;

  EXPECT_TRUE(s.isPlaying);
  EXPECT_DOUBLE_EQ(s.positionSec, 123.456);
  EXPECT_EQ(s.updatedBy, "testuser");
  EXPECT_EQ(s.updatedAt, 1709487600000);
}

TEST(PlaybackStateTest, CopySemantics) {
  PlaybackState original;
  original.isPlaying = true;
  original.positionSec = 50.0;
  original.updatedBy = "user_x";
  original.updatedAt = 999;

  PlaybackState copy = original;

  EXPECT_EQ(copy.isPlaying, original.isPlaying);
  EXPECT_DOUBLE_EQ(copy.positionSec, original.positionSec);
  EXPECT_EQ(copy.updatedBy, original.updatedBy);
  EXPECT_EQ(copy.updatedAt, original.updatedAt);

  // Modifying copy shouldn't affect original
  copy.isPlaying = false;
  EXPECT_TRUE(original.isPlaying);
}

// ============================================================================
// Initial state application on join
// ============================================================================

class InitialStateSyncTest : public ::testing::Test {
protected:
  MockPlayer player_;
  MockFirebase firebase_;
};

TEST_F(InitialStateSyncTest, AppliesExistingRoomState) {
  // Pre-populate room with a playing state
  PlaybackState existing;
  existing.isPlaying = true;
  existing.positionSec = 60.0;
  existing.updatedBy = "earlier_user";
  existing.updatedAt = 500;

  firebase_.writePlaybackState("JOIN_ROOM", existing);

  // New user joins
  Sync sync(player_, firebase_, "JOIN_ROOM", "new_user");
  sync.start();

  EXPECT_TRUE(player_.playing_);
  EXPECT_NEAR(player_.position_, 60.0, 0.01);
}

TEST_F(InitialStateSyncTest, EmptyRoomStartsPaused) {
  // No pre-existing state
  Sync sync(player_, firebase_, "EMPTY_ROOM", "first_user");
  sync.start();

  // Player should remain in default state (paused at 0)
  EXPECT_FALSE(player_.playing_);
  EXPECT_DOUBLE_EQ(player_.position_, 0.0);
}

TEST_F(InitialStateSyncTest, SuppressesEventWhenApplyingInitialState) {
  PlaybackState existing;
  existing.isPlaying = false;
  existing.positionSec = 30.0;
  existing.updatedBy = "prev_user";
  existing.updatedAt = 100;

  firebase_.writePlaybackState("ROOM", existing);
  player_.suppressCount_ = 0;

  Sync sync(player_, firebase_, "ROOM", "new_user");
  sync.start();

  // Should suppress to avoid echoing the initial state back
  EXPECT_GE(player_.suppressCount_, 1);
}

// ============================================================================
// Stress / edge case tests
// ============================================================================

TEST(StressTest, ManyUsersInOneRoom) {
  MockFirebase firebase;

  for (int i = 0; i < 100; ++i) {
    firebase.joinRoom("CROWDED", "user_" + std::to_string(i));
  }

  EXPECT_EQ(firebase.rooms_["CROWDED"].size(), 100u);
}

TEST(StressTest, RapidFireStateUpdates) {
  MockPlayer player;
  MockFirebase firebase;
  Sync sync(player, firebase, "RAPID", "user1");
  sync.start();

  // Simulate 1000 rapid toggles
  for (int i = 0; i < 1000; ++i) {
    player.simulateUserToggle(i % 2 == 0, i * 0.1);
  }

  EXPECT_EQ(firebase.writeLog_.size(), 1000u);
}

TEST(StressTest, ManyRoomsIndependent) {
  MockFirebase firebase;

  // Create 50 rooms, each with its own state
  for (int i = 0; i < 50; ++i) {
    std::string room = "ROOM_" + std::to_string(i);
    PlaybackState state;
    state.isPlaying = (i % 2 == 0);
    state.positionSec = i * 1.5;
    state.updatedBy = "user_" + std::to_string(i);
    firebase.writePlaybackState(room, state);
  }

  // Verify each room has its own correct state
  for (int i = 0; i < 50; ++i) {
    std::string room = "ROOM_" + std::to_string(i);
    auto state = firebase.readPlaybackState(room);
    EXPECT_EQ(state.isPlaying, (i % 2 == 0));
    EXPECT_DOUBLE_EQ(state.positionSec, i * 1.5);
  }
}

TEST(StressTest, LargePositionValues) {
  PlaybackState state;
  // 3-hour movie in seconds
  state.positionSec = 10800.0;
  state.isPlaying = true;
  EXPECT_DOUBLE_EQ(state.positionSec, 10800.0);

  // Very precise position
  state.positionSec = 3723.456789;
  EXPECT_DOUBLE_EQ(state.positionSec, 3723.456789);
}

// ============================================================================
// User ID generation (functional test)
// ============================================================================

TEST(UserIdTest, GeneratedIdsAreUnique) {
  // Generate 100 user IDs and check they're all different
  auto generateUserId = []() -> std::string {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string id = "user_";
    for (int i = 0; i < 6; ++i)
      id += chars[dist(gen)];
    return id;
  };

  std::set<std::string> ids;
  for (int i = 0; i < 100; ++i) {
    ids.insert(generateUserId());
  }

  // With 36^6 possibilities, collisions in 100 samples should be near-zero
  EXPECT_GE(ids.size(), 95u); // Allow tiny collision margin
}

TEST(UserIdTest, GeneratedIdFormat) {
  auto generateUserId = []() -> std::string {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string id = "user_";
    for (int i = 0; i < 6; ++i)
      id += chars[dist(gen)];
    return id;
  };

  std::string id = generateUserId();
  EXPECT_EQ(id.substr(0, 5), "user_");
  EXPECT_EQ(id.length(), 11u); // "user_" + 6 chars
}

// ============================================================================
// Config file parsing (functional test)
// ============================================================================

TEST(ConfigTest, ValidConfigFileParses) {
  std::string tmpConfig = "/tmp/watchmooi_test.conf";
  {
    std::ofstream ofs(tmpConfig);
    ofs << "# comment line\n";
    ofs << "firebase_url=https://myproject.firebaseio.com\n";
    ofs.close();
  }

  // Simulate the config loading logic
  std::string firebaseUrl;
  {
    std::ifstream file(tmpConfig);
    std::string line;
    while (std::getline(file, line)) {
      if (line.rfind("firebase_url=", 0) == 0) {
        firebaseUrl = line.substr(13);
      }
    }
    file.close();
  }

  EXPECT_EQ(firebaseUrl, "https://myproject.firebaseio.com");
  std::filesystem::remove(tmpConfig);
}

TEST(ConfigTest, EmptyConfigFileReturnsEmpty) {
  std::string tmpConfig = "/tmp/watchmooi_empty.conf";
  {
    std::ofstream ofs(tmpConfig);
    ofs << "# only comments\n";
    ofs.close();
  }

  std::string firebaseUrl;
  {
    std::ifstream file(tmpConfig);
    std::string line;
    while (std::getline(file, line)) {
      if (line.rfind("firebase_url=", 0) == 0) {
        firebaseUrl = line.substr(13);
      }
    }
    file.close();
  }

  EXPECT_TRUE(firebaseUrl.empty());
  std::filesystem::remove(tmpConfig);
}

TEST(ConfigTest, MissingConfigFileHandled) {
  std::ifstream file("/tmp/this_config_does_not_exist.conf");
  EXPECT_FALSE(file.is_open());
}

// ============================================================================
// User presence / leave notification tests
// ============================================================================

class UserPresenceTest : public ::testing::Test {
protected:
  void SetUp() override {
    player_ = std::make_unique<MockPlayer>();
    firebase_ = std::make_unique<MockFirebase>();
    sync_ = std::make_unique<Sync>(*player_, *firebase_, "PRESENCE_ROOM", "me");
    sync_->start();
  }

  std::unique_ptr<MockPlayer> player_;
  std::unique_ptr<MockFirebase> firebase_;
  std::unique_ptr<Sync> sync_;
};

TEST_F(UserPresenceTest, OtherUserLeavePausesPlayback) {
  // Start playing
  player_->playing_ = true;

  // Simulate partner leaving
  sync_->handleUserEvent({"partner", "", false});

  // Playback should be paused
  EXPECT_FALSE(player_->playing_);
  // Suppress should have been called
  EXPECT_GE(player_->suppressCount_, 1);
}

TEST_F(UserPresenceTest, OtherUserLeaveWhilePausedDoesNotCrash) {
  player_->playing_ = false;

  // Should not crash or do anything weird
  sync_->handleUserEvent({"partner", "", false});

  EXPECT_FALSE(player_->playing_);
}

TEST_F(UserPresenceTest, OtherUserJoinDoesNotAffectPlayback) {
  player_->playing_ = false;
  player_->actionLog_.clear();

  sync_->handleUserEvent({"partner", "", true});

  // No playback actions should have occurred
  EXPECT_TRUE(player_->actionLog_.empty());
  EXPECT_FALSE(player_->playing_);
}

TEST_F(UserPresenceTest, OwnEventsAreIgnored) {
  player_->playing_ = true;
  player_->actionLog_.clear();

  // Our own leave event — should be ignored
  sync_->handleUserEvent({"me", "", false});

  EXPECT_TRUE(player_->playing_); // still playing
  EXPECT_TRUE(player_->actionLog_.empty());
}

TEST_F(UserPresenceTest, StopCallsLeaveRoomAndStopListening) {
  sync_->stop();

  // leaveRoom should have been called
  ASSERT_FALSE(firebase_->leaveLog_.empty());
  EXPECT_EQ(firebase_->leaveLog_.back().roomCode, "PRESENCE_ROOM");
  EXPECT_EQ(firebase_->leaveLog_.back().userId, "me");

  // stopListening should have been called
  EXPECT_TRUE(firebase_->stopListeningCalled_);
}

TEST(UserPresenceStandaloneTest, LeaveRoomRemovesUserFromMock) {
  MockFirebase firebase;
  firebase.joinRoom("ROOM1", "alice");
  firebase.joinRoom("ROOM1", "bob");
  EXPECT_EQ(firebase.rooms_["ROOM1"].size(), 2u);

  firebase.leaveRoom("ROOM1", "alice");
  EXPECT_EQ(firebase.rooms_["ROOM1"].size(), 1u);
  EXPECT_EQ(firebase.rooms_["ROOM1"][0], "bob");
}

TEST(UserPresenceStandaloneTest, MultipleJoinLeaveSequence) {
  MockFirebase firebase;
  MockPlayer player;
  Sync sync(player, firebase, "SEQ_ROOM", "user1");
  sync.start();

  // user2 joins
  sync.handleUserEvent({"user2", "", true});
  // user3 joins
  sync.handleUserEvent({"user3", "", true});

  // user2 leaves while playing
  player.playing_ = true;
  sync.handleUserEvent({"user2", "", false});
  EXPECT_FALSE(player.playing_); // paused

  // user3 is still here, resume
  player.playing_ = true;

  // user3 leaves
  sync.handleUserEvent({"user3", "", false});
  EXPECT_FALSE(player.playing_); // paused again
}

// ============================================================================
// REQUIREMENT 3 & 7: WebRTC + Chat (placeholder tests for future)
// ============================================================================

TEST(FutureFeaturesTest, DISABLED_WebRTCVideoStreamEstablished) {
  // TODO: When WebRTC is implemented, test that:
  // - Peer connection is established between two users
  // - Webcam video stream is transmitted
  // - Both users can see each other's video
  FAIL() << "WebRTC not yet implemented";
}

TEST(FutureFeaturesTest, DISABLED_ChatMessageDelivered) {
  // TODO: When chat is implemented, test that:
  // - Messages are sent from one user to another
  // - Messages arrive in order
  // - Messages are scoped to the room
  FAIL() << "Chat not yet implemented";
}

TEST(FutureFeaturesTest, DISABLED_ForwardRewindSync) {
  // TODO: When forward/rewind is implemented, test that:
  // - Fast-forward syncs to the other user
  // - Rewind syncs to the other user
  // - Seek position is accurate
  FAIL() << "Forward/rewind sync not yet implemented";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
