#include "firebase.h"
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <iostream>

using json = nlohmann::json;

// ── PlaybackState JSON helpers ──────────────────────────────────────────────

json playbackStateToJson(const PlaybackState &s) {
  return json{{"isPlaying", s.isPlaying},
              {"positionSec", s.positionSec},
              {"updatedBy", s.updatedBy},
              {"updatedAt", s.updatedAt}};
}

PlaybackState playbackStateFromJson(const json &j) {
  PlaybackState s;
  if (j.is_null())
    return s;
  if (j.contains("isPlaying"))
    s.isPlaying = j["isPlaying"].get<bool>();
  if (j.contains("positionSec"))
    s.positionSec = j["positionSec"].get<double>();
  if (j.contains("updatedBy"))
    s.updatedBy = j["updatedBy"].get<std::string>();
  if (j.contains("updatedAt"))
    s.updatedAt = j["updatedAt"].get<int64_t>();
  return s;
}

// ── cURL write callback ─────────────────────────────────────────────────────

static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  auto *str = static_cast<std::string *>(userdata);
  str->append(ptr, size * nmemb);
  return size * nmemb;
}

// ── cURL progress callback (used to abort SSE when stopListening is called) ─

static int progressCallback(void *clientp, curl_off_t /*dltotal*/,
                            curl_off_t /*dlnow*/, curl_off_t /*ultotal*/,
                            curl_off_t /*ulnow*/) {
  auto *listening = static_cast<std::atomic<bool> *>(clientp);
  // Return non-zero to abort the transfer
  return listening->load() ? 0 : 1;
}

// ── SSE streaming callback ─────────────────────────────────────────────────

struct SSEContext {
  Firebase::StateCallback callback;
  std::atomic<bool> *listening;
  std::string buffer; // accumulate partial lines
};

static size_t sseCallback(char *ptr, size_t size, size_t nmemb,
                          void *userdata) {
  auto *ctx = static_cast<SSEContext *>(userdata);
  if (!ctx->listening->load())
    return 0; // returning 0 aborts the transfer

  size_t bytes = size * nmemb;
  ctx->buffer.append(ptr, bytes);

  // Process complete lines
  size_t pos;
  while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
    std::string line = ctx->buffer.substr(0, pos);
    ctx->buffer.erase(0, pos + 1);

    // Trim \r
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    // SSE data lines look like:  data: {"path":"/","data":{...}}
    if (line.rfind("data: ", 0) == 0) {
      std::string jsonStr = line.substr(6);

      try {
        json j = json::parse(jsonStr);

        // Firebase SSE wraps data in {"path":"/","data":{...}}
        json data;
        if (j.contains("data")) {
          data = j["data"];
        } else {
          data = j;
        }

        if (!data.is_null() && data.is_object()) {
          PlaybackState state = playbackStateFromJson(data);
          ctx->callback(state);
        }
      } catch (const json::exception &e) {
        std::cerr << "[Firebase] SSE parse error: " << e.what() << "\n";
      }
    }
  }

  return bytes;
}

// ── SSE user-presence callback ──────────────────────────────────────────────

struct UserSSEContext {
  Firebase::UserCallback callback;
  std::atomic<bool> *listening;
  std::set<std::string> *knownUsers;
  std::string buffer;
};

static size_t userSseCallback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
  auto *ctx = static_cast<UserSSEContext *>(userdata);
  if (!ctx->listening->load())
    return 0;

  size_t bytes = size * nmemb;
  ctx->buffer.append(ptr, bytes);

  size_t pos;
  while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
    std::string line = ctx->buffer.substr(0, pos);
    ctx->buffer.erase(0, pos + 1);

    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line.rfind("data: ", 0) == 0) {
      std::string jsonStr = line.substr(6);

      try {
        json j = json::parse(jsonStr);
        json data;
        if (j.contains("data")) {
          data = j["data"];
        } else {
          data = j;
        }

        if (data.is_null()) {
          // All users removed — everyone left
          for (const auto &uid : *ctx->knownUsers) {
            ctx->callback({uid, false});
          }
          ctx->knownUsers->clear();
        } else if (data.is_object()) {
          // Full user list — diff against known users
          std::set<std::string> currentUsers;
          for (auto &[uid, val] : data.items()) {
            currentUsers.insert(uid);
          }

          // Detect joins
          for (const auto &uid : currentUsers) {
            if (ctx->knownUsers->find(uid) == ctx->knownUsers->end()) {
              ctx->callback({uid, true});
            }
          }

          // Detect leaves
          for (const auto &uid : *ctx->knownUsers) {
            if (currentUsers.find(uid) == currentUsers.end()) {
              ctx->callback({uid, false});
            }
          }

          *ctx->knownUsers = currentUsers;
        }
      } catch (const json::exception &e) {
        std::cerr << "[Firebase] User SSE parse error: " << e.what() << "\n";
      }
    }
  }

  return bytes;
}

// ── Firebase class ──────────────────────────────────────────────────────────

Firebase::Firebase(const std::string &dbUrl) : dbUrl_(dbUrl) {
  // Strip trailing slash
  if (!dbUrl_.empty() && dbUrl_.back() == '/')
    dbUrl_.pop_back();

  curl_global_init(CURL_GLOBAL_DEFAULT);
}

Firebase::~Firebase() {
  stopListening();
  curl_global_cleanup();
}

bool Firebase::joinRoom(const std::string &roomCode,
                        const std::string &userId) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  json body = {{"joinedAt", now}};

  std::string url =
      dbUrl_ + "/rooms/" + roomCode + "/users/" + userId + ".json";
  std::string response = httpPut(url, body.dump());

  if (response.empty()) {
    std::cerr << "[Firebase] Failed to join room " << roomCode << "\n";
    return false;
  }

  std::cout << "[Firebase] Joined room " << roomCode << " as " << userId
            << "\n";
  return true;
}

bool Firebase::leaveRoom(const std::string &roomCode,
                         const std::string &userId) {
  std::string url =
      dbUrl_ + "/rooms/" + roomCode + "/users/" + userId + ".json";
  std::string response = httpDelete(url);

  if (response.empty()) {
    std::cerr << "[Firebase] Failed to leave room " << roomCode << "\n";
    return false;
  }

  std::cout << "[Firebase] Left room " << roomCode << " (" << userId << ")\n";
  return true;
}

bool Firebase::writePlaybackState(const std::string &roomCode,
                                  const PlaybackState &state) {
  json body = playbackStateToJson(state);
  std::string url = dbUrl_ + "/rooms/" + roomCode + "/playback.json";
  std::string response = httpPut(url, body.dump());

  if (response.empty()) {
    std::cerr << "[Firebase] Failed to write playback state\n";
    return false;
  }
  return true;
}

PlaybackState Firebase::readPlaybackState(const std::string &roomCode) {
  std::string url = dbUrl_ + "/rooms/" + roomCode + "/playback.json";
  std::string response = httpGet(url);

  if (response.empty() || response == "null")
    return {};

  try {
    json j = json::parse(response);
    return playbackStateFromJson(j);
  } catch (...) {
    return {};
  }
}

void Firebase::listenForChanges(const std::string &roomCode, StateCallback cb) {
  listening_ = true;

  listenerThread_ = std::thread([this, roomCode, cb]() {
    std::string url = dbUrl_ + "/rooms/" + roomCode + "/playback.json";

    while (listening_.load()) {
      CURL *curl = curl_easy_init();
      if (!curl) {
        std::cerr << "[Firebase] SSE: curl_easy_init failed\n";
        break;
      }

      SSEContext ctx;
      ctx.callback = cb;
      ctx.listening = &listening_;

      struct curl_slist *headers = nullptr;
      headers = curl_slist_append(headers, "Accept: text/event-stream");

      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sseCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

      // Progress callback to abort when stopListening() is called
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &listening_);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

      CURLcode res = curl_easy_perform(curl);
      if (res != CURLE_OK && res != CURLE_ABORTED_BY_CALLBACK &&
          listening_.load()) {
        std::cerr << "[Firebase] SSE connection lost: "
                  << curl_easy_strerror(res) << ". Reconnecting in 2s...\n";
      }

      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);

      // Reconnect with backoff if still listening
      if (listening_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }
  });
}

void Firebase::listenForUserChanges(const std::string &roomCode,
                                    UserCallback cb) {
  // Read initial user list
  std::string initUrl = dbUrl_ + "/rooms/" + roomCode + "/users.json";
  std::string initResp = httpGet(initUrl);
  if (!initResp.empty() && initResp != "null") {
    try {
      json j = json::parse(initResp);
      if (j.is_object()) {
        for (auto &[uid, val] : j.items()) {
          knownUsers_.insert(uid);
        }
      }
    } catch (...) {
    }
  }

  userListenerThread_ = std::thread([this, roomCode, cb]() {
    std::string url = dbUrl_ + "/rooms/" + roomCode + "/users.json";

    while (listening_.load()) {
      CURL *curl = curl_easy_init();
      if (!curl) {
        std::cerr << "[Firebase] User SSE: curl_easy_init failed\n";
        break;
      }

      UserSSEContext ctx;
      ctx.callback = cb;
      ctx.listening = &listening_;
      ctx.knownUsers = &knownUsers_;

      struct curl_slist *headers = nullptr;
      headers = curl_slist_append(headers, "Accept: text/event-stream");

      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, userSseCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

      // Progress callback to abort when stopListening() is called
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &listening_);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

      CURLcode res = curl_easy_perform(curl);
      if (res != CURLE_OK && res != CURLE_ABORTED_BY_CALLBACK &&
          listening_.load()) {
        std::cerr << "[Firebase] User SSE lost: " << curl_easy_strerror(res)
                  << ". Reconnecting in 2s...\n";
      }

      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);

      if (listening_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }
  });
}

void Firebase::stopListening() {
  listening_ = false;
  if (listenerThread_.joinable())
    listenerThread_.join();
  if (userListenerThread_.joinable())
    userListenerThread_.join();
}

// ── HTTP helpers ────────────────────────────────────────────────────────────

std::string Firebase::httpPut(const std::string &url, const std::string &body) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    std::cerr << "[Firebase] PUT error: " << curl_easy_strerror(res) << "\n";
    response.clear();
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return response;
}

std::string Firebase::httpGet(const std::string &url) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    std::cerr << "[Firebase] GET error: " << curl_easy_strerror(res) << "\n";
    response.clear();
  }

  curl_easy_cleanup(curl);
  return response;
}

std::string Firebase::httpDelete(const std::string &url) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    std::cerr << "[Firebase] DELETE error: " << curl_easy_strerror(res) << "\n";
    response.clear();
  }

  curl_easy_cleanup(curl);
  return response;
}
