#include "firebase.h"
#include "player.h"
#include "sync.h"

#include <fstream>
#include <iostream>
#include <random>
#include <string>

// ── Config ──────────────────────────────────────────────────────────────────

static const std::string CONFIG_FILE = "watchmooi.conf";

static std::string loadFirebaseUrl() {
  // Try config file first
  std::ifstream file(CONFIG_FILE);
  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) {
      // Simple key=value parsing
      if (line.rfind("firebase_url=", 0) == 0) {
        return line.substr(13);
      }
    }
  }
  return "";
}

// ── Helpers ─────────────────────────────────────────────────────────────────

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

static void printUsage(const char *progName) {
  std::cout << "Usage: " << progName
            << " --room <ROOM_CODE> --video <FILE_PATH>"
            << " [--firebase-url <URL>]\n\n"
            << "Options:\n"
            << "  --room         Room code to join (both users must use the "
               "same code)\n"
            << "  --video        Path to the local video file\n"
            << "  --firebase-url Firebase Realtime Database URL\n"
            << "                 (can also be set in watchmooi.conf as "
               "firebase_url=...)\n";
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  std::string roomCode;
  std::string videoPath;
  std::string firebaseUrl;
  std::string displayName;

  // Parse CLI args
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--room" && i + 1 < argc) {
      roomCode = argv[++i];
    } else if (arg == "--video" && i + 1 < argc) {
      videoPath = argv[++i];
    } else if (arg == "--firebase-url" && i + 1 < argc) {
      firebaseUrl = argv[++i];
    } else if (arg == "--name" && i + 1 < argc) {
      displayName = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
  }

  // Interactive prompts for missing args
  if (firebaseUrl.empty())
    firebaseUrl = loadFirebaseUrl();

  if (firebaseUrl.empty()) {
    std::cout << "Firebase Realtime Database URL: ";
    std::getline(std::cin, firebaseUrl);
  }

  if (roomCode.empty()) {
    std::cout << "Room code: ";
    std::getline(std::cin, roomCode);
  }

  if (videoPath.empty()) {
    std::cout << "Video file path: ";
    std::getline(std::cin, videoPath);
  }

  if (firebaseUrl.empty() || roomCode.empty() || videoPath.empty()) {
    std::cerr
        << "Error: Firebase URL, room code, and video path are required.\n";
    printUsage(argv[0]);
    return 1;
  }

  if (displayName.empty()) {
    std::cout << "Your name (press Enter to skip): ";
    std::getline(std::cin, displayName);
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
    // Initialise components
    Player player;
    Firebase firebase(firebaseUrl);
    Sync sync(player, firebase, roomCode, userId);

    // Join the room
    if (!firebase.joinRoom(roomCode, userId, displayName)) {
      std::cerr
          << "Failed to join room. Check your Firebase URL and network.\n";
      return 1;
    }

    // Load video file
    if (!player.loadFile(videoPath)) {
      std::cerr << "Failed to load video: " << videoPath << "\n";
      return 1;
    }

    // Start sync (wires up callbacks + SSE listener)
    sync.start();

    // Run the mpv event loop (blocks until window is closed)
    player.runLoop();

    // Clean shutdown: stop listeners + leave room
    sync.stop();

    std::cout << "Goodbye!\n";
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
