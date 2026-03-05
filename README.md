# watchMooi 🎬

Synchronized video playback for two people. Join a room by code, play a local video, and any play/pause action is mirrored in real-time via Firebase.

```
┌──────────┐                              ┌──────────┐
│  User 1  │◄──── Firebase RTDB ────►│  User 2  │
│  (C++)   │      (middleman)        │  (C++)   │
└──────────┘                              └──────────┘
```

---

## Features (MVP)

- ⏯️ Synced play/pause between two users
- 🔑 Room codes — both users enter the same code to connect
- 📍 Position sync — seeks to match if drift > 1 second
- 🔒 Room isolation — each room is private
- 🖥️ Cross-platform C++17 codebase

---

## Requirements

| Dependency                     | Purpose                      |
| ------------------------------ | ---------------------------- |
| **CMake** ≥ 3.16               | Build system                 |
| **g++** or **clang++** (C++17) | Compiler                     |
| **libmpv-dev**                 | Video playback               |
| **libcurl-dev** (with SSL)     | HTTP + SSE to Firebase       |
| **pkg-config**                 | Finds libmpv                 |
| **Git**                        | Cloning + CMake FetchContent |

> **nlohmann/json** and **Google Test** are fetched automatically by CMake — no manual install needed.

---

## Install Dependencies

### 🐧 Arch Linux / Manjaro / EndeavourOS

```bash
sudo pacman -S cmake base-devel mpv curl git pkg-config
```

### 🐧 Ubuntu / Debian / Linux Mint / Pop!\_OS

```bash
sudo apt update
sudo apt install cmake build-essential libmpv-dev libcurl4-openssl-dev pkg-config git
```

### 🐧 Fedora / RHEL / CentOS Stream / Rocky Linux

```bash
sudo dnf install cmake gcc-c++ mpv-libs-devel libcurl-devel pkg-config git
```

On older RHEL/CentOS (no mpv in base repos):

```bash
sudo dnf install epel-release
sudo dnf install --enablerepo=rpmfusion-free mpv-libs-devel
```

### 🐧 openSUSE (Tumbleweed / Leap)

```bash
sudo zypper install cmake gcc-c++ mpv-devel libcurl-devel pkg-config git
```

### 🐧 Void Linux

```bash
sudo xbps-install cmake gcc mpv-devel libcurl-devel pkg-config git
```

### 🐧 Alpine Linux

```bash
sudo apk add cmake g++ mpv-dev curl-dev pkgconfig git make
```

### 🐧 NixOS / Nix

```bash
nix-shell -p cmake gcc mpv curl pkg-config git
```

Or add to your `shell.nix`:

```nix
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = [ pkgs.cmake pkgs.gcc pkgs.mpv pkgs.curl pkgs.pkg-config pkgs.git ];
}
```

### 🍎 macOS (Homebrew)

```bash
brew install cmake mpv curl pkg-config git
```

> macOS ships with a C++ compiler (clang) via Xcode CLI tools:
>
> ```bash
> xcode-select --install
> ```

### 🪟 Windows (MSYS2)

1. Install [MSYS2](https://www.msys2.org/)
2. Open **MSYS2 UCRT64** terminal:

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-mpv mingw-w64-ucrt-x86_64-curl \
          mingw-w64-ucrt-x86_64-pkg-config git make
```

---

## Firebase Setup

The app uses **Firebase Realtime Database** as the sync server. No backend code needed.

### 1. Create a Firebase project

1. Go to [Firebase Console](https://console.firebase.google.com/)
2. Click **Add project** → name it (e.g., `watchmooi`)
3. Disable Google Analytics (not needed) → **Create project**

### 2. Enable Realtime Database

1. In the sidebar: **Build → Realtime Database**
2. Click **Create Database**
3. Choose a region → **Start in test mode**
4. Your database URL will appear. **Copy it exactly** — the format depends on your region:

   | Region        | URL format                                                      |
   | ------------- | --------------------------------------------------------------- |
   | US (default)  | `https://YOUR-PROJECT-default-rtdb.firebaseio.com`              |
   | Other regions | `https://YOUR-PROJECT-default-rtdb.REGION.firebasedatabase.app` |

   For example, Asia Southeast 1:

   ```
   https://watchmooi-default-rtdb.asia-southeast1.firebasedatabase.app
   ```

> ⚠️ **Using the wrong URL format is the #1 setup mistake.** If you chose a non-US region, you **must** use the `.firebasedatabase.app` domain, not `.firebaseio.com`.

### 3. Set security rules (for development)

Go to **Realtime Database → Rules** and set:

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

> ⚠️ **These are open rules for development only.** Lock them down before sharing publicly.

---

## Build

```bash
git clone <your-repo-url>
cd watchMooi

mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces two binaries:

- `watchmooi` — the main app
- `watchmooi_tests` — the test suite

---

## Configure

Edit `watchmooi.conf` in the project root:

```
firebase_url=https://YOUR-PROJECT-default-rtdb.REGION.firebasedatabase.app
```

Or pass it directly via CLI:

```bash
./watchmooi --firebase-url https://YOUR-PROJECT-default-rtdb.REGION.firebasedatabase.app
```

> **Note:** Copy the `watchmooi.conf` into the `build/` directory, or run the binary from the project root.

---

## Run

### Basic usage

```bash
./watchmooi --room MYROOM --video /path/to/movie.mp4
```

### Interactive mode (prompts for missing args)

```bash
./watchmooi
# Firebase Realtime Database URL: https://...
# Room code: MYROOM
# Video file path: /home/user/movie.mp4
```

### Two-user test (same machine, two terminals)

**Terminal 1:**

```bash
cd build
./watchmooi --room PIZZA --video ~/Videos/movie.mkv
```

**Terminal 2:**

```bash
cd build
./watchmooi --room PIZZA --video ~/Videos/movie.mkv
```

### Controls

| Key     | Action                              |
| ------- | ----------------------------------- |
| `SPACE` | Play / Pause (synced to other user) |
| `Q`     | Quit                                |

### What happens

1. Both users enter the same room code
2. An mpv video window opens (starts paused)
3. When **either user** presses SPACE → both players play/pause together
4. If playback positions drift by more than 1 second, they auto-resync

---

## Run Tests

```bash
cd build
make watchmooi_tests -j$(nproc)
./watchmooi_tests
```

Expected output:

```
[==========] 48 tests from 11 test suites ran.
[  PASSED  ] 48 tests.
  YOU HAVE 3 DISABLED TESTS
```

The 3 disabled tests are placeholders for future features (WebRTC, chat, forward/rewind).

---

## Project Structure

```
watchMooi/
├── CMakeLists.txt        # Build config
├── README.md             # This file
├── watchmooi.conf        # Firebase URL config
├── src/
│   ├── interfaces.h      # IPlayer / IFirebase abstractions
│   ├── player.h / .cpp   # libmpv video player wrapper
│   ├── firebase.h / .cpp # Firebase REST + SSE client
│   ├── sync.h / .cpp     # Glue: local ↔ remote sync
│   └── main.cpp          # Entry point + CLI
└── tests/
    └── tests.cpp         # 48 unit tests (Google Test)
```

---

## Troubleshooting

### "Failed to join room. Check your Firebase URL and network."

- Verify your `firebase_url` in `watchmooi.conf` matches your Firebase Console URL **exactly**
- If your DB is in a non-US region, make sure you're using `.firebasedatabase.app`, not `.firebaseio.com`
- Check that your Firebase RTDB rules allow read/write
- Make sure you have internet connectivity
- Make sure `watchmooi.conf` is in the directory you run the binary from (e.g., `build/`)

### mpv window doesn't open

- Check that mpv is installed: `mpv --version`
- Try a different video output: set `vo` in `player.cpp` to `"x11"` or `"wayland"`

### CMake can't find mpv

- Make sure `libmpv-dev` (or equivalent) is installed
- Check: `pkg-config --modversion mpv`

### Build fails on macOS with "mpv/client.h not found"

- Ensure mpv was installed via Homebrew: `brew install mpv`
- You may need: `export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"`

---

## Roadmap

- [ ] Forward / rewind sync
- [ ] WebRTC webcam video (see each other while watching)
- [ ] Text chat
- [ ] Auto-generated room codes
- [ ] Video file hash verification (ensure both users have the same file)
- [ ] Authentication (lock rooms to invited users)

---

## License

MIT
