#include "firebase.h"
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <filesystem>
#include <iostream>

// ── CA bundle auto-detection ────────────────────────────────────────────────

static std::string findCaBundlePath() {
  for (const char *env : {"CURL_CA_BUNDLE", "SSL_CERT_FILE"}) {
    if (const char *v = std::getenv(env); v && std::filesystem::exists(v))
      return v;
  }
  for (const char *p : {
           "C:\\msys64\\ucrt64\\etc\\ssl\\certs\\ca-bundle.crt",
           "C:/msys64/ucrt64/etc/ssl/certs/ca-bundle.crt",
           "C:\\msys64\\usr\\ssl\\certs\\ca-bundle.crt",
           "C:/msys64/usr/ssl/certs/ca-bundle.crt",
           "/ucrt64/etc/ssl/certs/ca-bundle.crt",
           "/usr/ssl/certs/ca-bundle.crt",
           "/etc/ssl/certs/ca-certificates.crt",
       }) {
    if (std::filesystem::exists(p))
      return p;
  }
  return "";
}

static const std::string CA_BUNDLE_PATH = findCaBundlePath();

static void setCurlSSL(CURL *curl) {
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  if (!CA_BUNDLE_PATH.empty())
    curl_easy_setopt(curl, CURLOPT_CAINFO, CA_BUNDLE_PATH.c_str());
}

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

// ── cURL callbacks ──────────────────────────────────────────────────────────

static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *ud) {
  static_cast<std::string *>(ud)->append(ptr, size * nmemb);
  return size * nmemb;
}

static int progressCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t,
                            curl_off_t) {
  return static_cast<std::atomic<bool> *>(clientp)->load() ? 0 : 1;
}

// ── SSE helpers ─────────────────────────────────────────────────────────────

// Parse complete lines out of an SSE buffer, calling onData(json) for each.
static void parseSseLines(std::string &buf,
                          const std::function<void(const json &)> &onData) {
  size_t pos;
  while ((pos = buf.find('\n')) != std::string::npos) {
    std::string line = buf.substr(0, pos);
    buf.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line.rfind("data: ", 0) != 0)
      continue;
    try {
      json j = json::parse(line.substr(6));
      json data = j.contains("data") ? j["data"] : j;
      onData(data);
    } catch (const json::exception &e) {
      std::cerr << "[Firebase] SSE parse error: " << e.what() << "\n";
    }
  }
}

// Run an SSE loop with reconnect. Calls writeFunc for each chunk.
static void runSSELoop(const std::string &url, std::atomic<bool> &listening,
                       curl_write_callback writeFunc, void *writeData) {
  while (listening.load()) {
    CURL *curl = curl_easy_init();
    if (!curl) {
      std::cerr << "[Firebase] SSE: curl_easy_init failed\n";
      break;
    }
    struct curl_slist *headers =
        curl_slist_append(nullptr, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, writeData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    setCurlSSL(curl);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &listening);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK && res != CURLE_ABORTED_BY_CALLBACK &&
        listening.load()) {
      std::cerr << "[Firebase] SSE lost: " << curl_easy_strerror(res)
                << ". Reconnecting in 2s...\n";
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (listening.load())
      std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

// ── SSE streaming callbacks ─────────────────────────────────────────────────

struct SSEContext {
  Firebase::StateCallback callback;
  std::atomic<bool> *listening;
  std::string buffer;
};

static size_t sseCallback(char *ptr, size_t size, size_t nmemb, void *ud) {
  auto *ctx = static_cast<SSEContext *>(ud);
  if (!ctx->listening->load())
    return 0;
  size_t bytes = size * nmemb;
  ctx->buffer.append(ptr, bytes);
  parseSseLines(ctx->buffer, [&](const json &data) {
    if (!data.is_null() && data.is_object())
      ctx->callback(playbackStateFromJson(data));
  });
  return bytes;
}

struct UserSSEContext {
  Firebase::UserCallback callback;
  std::atomic<bool> *listening;
  std::map<std::string, std::string> *knownUsers;
  std::string buffer;
};

static size_t userSseCallback(char *ptr, size_t size, size_t nmemb, void *ud) {
  auto *ctx = static_cast<UserSSEContext *>(ud);
  if (!ctx->listening->load())
    return 0;
  size_t bytes = size * nmemb;
  ctx->buffer.append(ptr, bytes);

  parseSseLines(ctx->buffer, [&](const json &data) {
    if (data.is_null()) {
      for (const auto &[uid, name] : *ctx->knownUsers)
        ctx->callback({uid, name, false});
      ctx->knownUsers->clear();
    } else if (data.is_object()) {
      std::map<std::string, std::string> current;
      for (auto &[uid, val] : data.items()) {
        std::string name;
        if (val.is_object() && val.contains("name"))
          name = val["name"].get<std::string>();
        current[uid] = name;
      }
      for (const auto &[uid, name] : current)
        if (ctx->knownUsers->find(uid) == ctx->knownUsers->end())
          ctx->callback({uid, name, true});
      for (const auto &[uid, name] : *ctx->knownUsers)
        if (current.find(uid) == current.end())
          ctx->callback({uid, name, false});
      *ctx->knownUsers = current;
    }
  });
  return bytes;
}

// ── Firebase class ──────────────────────────────────────────────────────────

Firebase::Firebase(const std::string &dbUrl) : dbUrl_(dbUrl) {
  if (!dbUrl_.empty() && dbUrl_.back() == '/')
    dbUrl_.pop_back();
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

Firebase::~Firebase() {
  stopListening();
  curl_global_cleanup();
}

bool Firebase::joinRoom(const std::string &roomCode, const std::string &userId,
                        const std::string &displayName) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  json body = {{"joinedAt", now}};
  if (!displayName.empty())
    body["name"] = displayName;

  std::string url =
      dbUrl_ + "/rooms/" + roomCode + "/users/" + userId + ".json";
  std::string response = httpRequest(url, "PUT", body.dump());

  if (response.empty()) {
    std::cerr << "[Firebase] Failed to join room " << roomCode << "\n";
    return false;
  }

  std::string label = displayName.empty() ? userId : displayName;
  std::cout << "[Firebase] Joined room " << roomCode << " as " << label << "\n";
  return true;
}

bool Firebase::leaveRoom(const std::string &roomCode,
                         const std::string &userId) {
  std::string url =
      dbUrl_ + "/rooms/" + roomCode + "/users/" + userId + ".json";
  if (httpRequest(url, "DELETE").empty()) {
    std::cerr << "[Firebase] Failed to leave room " << roomCode << "\n";
    return false;
  }
  std::cout << "[Firebase] Left room " << roomCode << " (" << userId << ")\n";
  return true;
}

bool Firebase::writePlaybackState(const std::string &roomCode,
                                  const PlaybackState &state) {
  std::string url = dbUrl_ + "/rooms/" + roomCode + "/playback.json";
  if (httpRequest(url, "PUT", playbackStateToJson(state).dump()).empty()) {
    std::cerr << "[Firebase] Failed to write playback state\n";
    return false;
  }
  return true;
}

PlaybackState Firebase::readPlaybackState(const std::string &roomCode) {
  std::string resp =
      httpRequest(dbUrl_ + "/rooms/" + roomCode + "/playback.json", "GET");
  if (resp.empty() || resp == "null")
    return {};
  try {
    return playbackStateFromJson(json::parse(resp));
  } catch (...) {
    return {};
  }
}

void Firebase::listenForChanges(const std::string &roomCode, StateCallback cb) {
  listening_ = true;
  listenerThread_ = std::thread([this, roomCode, cb]() {
    std::string url = dbUrl_ + "/rooms/" + roomCode + "/playback.json";
    SSEContext ctx{cb, &listening_, {}};
    runSSELoop(url, listening_, sseCallback, &ctx);
  });
}

void Firebase::listenForUserChanges(const std::string &roomCode,
                                    UserCallback cb) {
  // Seed knownUsers_ from current DB state
  std::string initResp =
      httpRequest(dbUrl_ + "/rooms/" + roomCode + "/users.json", "GET");
  if (!initResp.empty() && initResp != "null") {
    try {
      json j = json::parse(initResp);
      if (j.is_object())
        for (auto &[uid, val] : j.items()) {
          std::string name;
          if (val.is_object() && val.contains("name"))
            name = val["name"].get<std::string>();
          knownUsers_[uid] = name;
        }
    } catch (...) {
    }
  }

  userListenerThread_ = std::thread([this, roomCode, cb]() {
    std::string url = dbUrl_ + "/rooms/" + roomCode + "/users.json";
    UserSSEContext ctx{cb, &listening_, &knownUsers_, {}};
    runSSELoop(url, listening_, userSseCallback, &ctx);
  });
}

void Firebase::stopListening() {
  listening_ = false;
  if (listenerThread_.joinable())
    listenerThread_.join();
  if (userListenerThread_.joinable())
    userListenerThread_.join();
}

// ── Unified HTTP helper ─────────────────────────────────────────────────────

std::string Firebase::httpRequest(const std::string &url,
                                  const std::string &method,
                                  const std::string &body) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;
  struct curl_slist *headers = nullptr;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  setCurlSSL(curl);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  if (method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty()) {
      headers = curl_slist_append(headers, "Content-Type: application/json");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    std::cerr << "[Firebase] " << method
              << " error: " << curl_easy_strerror(res) << "\n";
    response.clear();
  }

  if (headers)
    curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return response;
}
