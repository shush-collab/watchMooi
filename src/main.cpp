#include "firebase.h"
#include "player.h"
#include "sync.h"

#include <fstream>
#include <iostream>
#include <random>
#include <string>

// ── Helpers ─────────────────────────────────────────────────────────────────

static const std::string CONFIG_FILE = "watchmooi.conf";

static std::string loadFirebaseUrl() {
  std::ifstream file(CONFIG_FILE);
  std::string line;
  while (std::getline(file, line))
    if (line.rfind("firebase_url=", 0) == 0)
      return line.substr(13);
  return "";
}

static std::string generateUserId() {
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

  std::string id = "user_";
  for (int i = 0; i < 6; ++i)
    id += chars[dist(gen)];
  return id;
}

static void promptIfEmpty(std::string &field, const char *prompt) {
  if (field.empty()) {
    std::cout << prompt;
    std::getline(std::cin, field);
  }
}

static void printUsage(const char *prog) {
  std::cout << "Usage: " << prog
            << " --room <ROOM_CODE> --video <FILE_PATH>"
               " [--firebase-url <URL>] [--name <NAME>]\n";
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  std::string roomCode, videoPath, firebaseUrl, displayName;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--room" && i + 1 < argc)
      roomCode = argv[++i];
    else if (arg == "--video" && i + 1 < argc)
      videoPath = argv[++i];
    else if (arg == "--firebase-url" && i + 1 < argc)
      firebaseUrl = argv[++i];
    else if (arg == "--name" && i + 1 < argc)
      displayName = argv[++i];
    else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
  }

  if (firebaseUrl.empty())
    firebaseUrl = loadFirebaseUrl();

  promptIfEmpty(firebaseUrl, "Firebase Realtime Database URL: ");
  promptIfEmpty(roomCode, "Room code: ");
  promptIfEmpty(videoPath, "Video file path: ");

  if (firebaseUrl.empty() || roomCode.empty() || videoPath.empty()) {
    std::cerr
        << "Error: Firebase URL, room code, and video path are required.\n";
    printUsage(argv[0]);
    return 1;
  }

  std::string userId = generateUserId();
  std::string banner = displayName.empty() ? userId : displayName;

  std::cout << "╔══════════════════════════════════════╗\n"
            << "║          watchMooi MVP v0.1          ║\n"
            << "╠══════════════════════════════════════╣\n"
            << "║  Room:  " << roomCode
            << std::string(29 - roomCode.size(), ' ') << "║\n"
            << "║  Name:  " << banner << std::string(29 - banner.size(), ' ')
            << "║\n"
            << "╠══════════════════════════════════════╣\n"
            << "║  Controls:                           ║\n"
            << "║    SPACE  = play / pause             ║\n"
            << "║    Q      = quit                     ║\n"
            << "╚══════════════════════════════════════╝\n";

  try {
    Player player;
    Firebase firebase(firebaseUrl);
    Sync sync(player, firebase, roomCode, userId);

    if (!firebase.joinRoom(roomCode, userId, displayName)) {
      std::cerr
          << "Failed to join room. Check your Firebase URL and network.\n";
      return 1;
    }
    if (!player.loadFile(videoPath)) {
      std::cerr << "Failed to load video: " << videoPath << "\n";
      return 1;
    }

    sync.start();
    player.runLoop();
    sync.stop();

    std::cout << "Goodbye!\n";
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
