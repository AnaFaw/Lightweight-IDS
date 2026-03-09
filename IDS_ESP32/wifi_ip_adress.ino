#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include "ml_weights.h"

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

const unsigned long WINDOW_MS = 10000;
const unsigned long LEARN_MS = 30000;
const unsigned long BLOCK_BASE_MS = 30000;
const unsigned long BLOCK_MAX_MS = 10UL * 60UL * 1000UL;
const unsigned long COOLDOWN_MS = 10UL * 60UL * 1000UL;
const unsigned long THROTTLE_DELAY_MS = 200;

unsigned long windowStart = 0;
unsigned long bootAt = 0;

const int MAX_IPS = 8;
const float ALPHA = 0.2f;
const float K = 3.0f;

float muReq = 0.0f, varReq = 1.0f;
float muFail = 0.0f, varFail = 1.0f;
float muNF = 0.0f, varNF = 1.0f;

struct IpEntry {
  bool used = false;
  uint32_t ip = 0;

  unsigned long totalReq = 0;
  unsigned long totalLogin = 0;
  unsigned long totalFail = 0;
  unsigned long totalNotFound = 0;

  unsigned long winReq = 0;
  unsigned long winFail = 0;
  unsigned long winNotFound = 0;

  unsigned long blockedUntil = 0;
  int offense = 0;
  unsigned long lastOffenseAt = 0;
  bool throttled = false;
  unsigned long lastSeen = 0;
};

IpEntry ips[MAX_IPS];

static inline void updateEWMA(float x, float &mu, float &var) {
  float diff = x - mu;
  mu = ALPHA * x + (1.0f - ALPHA) * mu;
  var = ALPHA * (diff * diff) + (1.0f - ALPHA) * var;
  if (var < 1e-6f) var = 1e-6f;
}

static inline uint32_t ipToU32(const IPAddress& a) {
  return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | (uint32_t)a[3];
}

static inline String ipToString(uint32_t ip) {
  return String((ip >> 24) & 0xFF) + "." +
         String((ip >> 16) & 0xFF) + "." +
         String((ip >> 8) & 0xFF) + "." +
         String(ip & 0xFF);
}

int getIpSlot(uint32_t ip) {
  for (int i = 0; i < MAX_IPS; i++) {
    if (ips[i].used && ips[i].ip == ip) return i;
  }

  for (int i = 0; i < MAX_IPS; i++) {
    if (!ips[i].used) {
      ips[i] = IpEntry();
      ips[i].used = true;
      ips[i].ip = ip;
      return i;
    }
  }

  int lru = 0;
  unsigned long best = ips[0].lastSeen;
  for (int i = 1; i < MAX_IPS; i++) {
    if (ips[i].lastSeen < best) {
      best = ips[i].lastSeen;
      lru = i;
    }
  }

  ips[lru] = IpEntry();
  ips[lru].used = true;
  ips[lru].ip = ip;
  return lru;
}

bool isBlocked(IpEntry &e) {
  return millis() < e.blockedUntil;
}

void applyBlock(IpEntry &e, unsigned long blockMs, const char* reason, int score) {
  unsigned long now = millis();

  if (e.offense > 0 && (now - e.lastOffenseAt) > COOLDOWN_MS) {
    e.offense = 0;
  }

  e.offense++;
  e.lastOffenseAt = now;

  unsigned long scaled = blockMs * (1UL << (e.offense - 1));
  if (scaled > BLOCK_MAX_MS) scaled = BLOCK_MAX_MS;

  e.blockedUntil = now + scaled;

  Serial.printf("[ALERT] IP=%s score=%d REASON=%s -> BLOCK %lu ms (offense=%d)\n",
                ipToString(e.ip).c_str(), score, reason, scaled, e.offense);
}

float ml_predict(float reqRate, float failRate, float nfRate, float netErrRate) {
  float x[ML_N] = {reqRate, failRate, nfRate, netErrRate};
  float z = ML_B;
  for (int i = 0; i < ML_N; i++) {
    z += ML_W[i] * x[i];
  }
  return ml_sigmoid(z);
}

void evaluateWindowAndMitigate() {
  bool learning = (millis() - bootAt) < LEARN_MS;
  float winSec = WINDOW_MS / 1000.0f;

  unsigned long sumReq = 0, sumFail = 0, sumNF = 0;
  for (int i = 0; i < MAX_IPS; i++) {
    if (!ips[i].used) continue;
    sumReq += ips[i].winReq;
    sumFail += ips[i].winFail;
    sumNF += ips[i].winNotFound;
  }

  float reqRateTotal = sumReq / winSec;
  float failRateTotal = sumFail / winSec;
  float nfRateTotal = sumNF / winSec;

  float thReq = muReq + K * sqrtf(varReq);
  float thFail = muFail + K * sqrtf(varFail);
  float thNF = muNF + K * sqrtf(varNF);

  if (thReq < 0.2f) thReq = 0.2f;
  if (thFail < 0.05f) thFail = 0.05f;
  if (thNF < 0.05f) thNF = 0.05f;

  Serial.printf("[WIN] TOTAL reqRate=%.2f failRate=%.2f nfRate=%.2f | thReq=%.2f thFail=%.2f thNF=%.2f | learning=%s\n",
                reqRateTotal, failRateTotal, nfRateTotal,
                thReq, thFail, thNF,
                learning ? "true" : "false");

  bool anyMitigation = false;

  const float ML_P_THROTTLE = 0.65f;
  const float ML_P_BLOCK = 0.85f;

  for (int i = 0; i < MAX_IPS; i++) {
    if (!ips[i].used) continue;

    ips[i].throttled = false;

    float rReq = ips[i].winReq / winSec;
    float rFail = ips[i].winFail / winSec;
    float rNF = ips[i].winNotFound / winSec;

    Serial.printf("[IPWIN] ip=%s rReq=%.2f (winReq=%lu) rFail=%.2f rNF=%.2f\n",
                  ipToString(ips[i].ip).c_str(),
                  rReq, ips[i].winReq, rFail, rNF);

    float p = ml_predict(rReq, rFail, rNF, 0.0f);

    Serial.printf("[ML] ip=%s p_attack=%.2f\n",
                  ipToString(ips[i].ip).c_str(), p);

    if (learning) {
      Serial.printf("[LEARN] skipping mitigation for ip=%s\n",
                    ipToString(ips[i].ip).c_str());
      continue;
    }

    if (!isBlocked(ips[i])) {
      if (p >= ML_P_BLOCK) {
        const char* reason = "ML_ATTACK";

        if (rFail > thFail) reason = "ML_BRUTE_FORCE";
        else if (rNF > thNF) reason = "ML_SCAN";
        else if (rReq > thReq) reason = "ML_FLOOD";

        applyBlock(ips[i], BLOCK_BASE_MS, reason, (int)(p * 100.0f));
        anyMitigation = true;
      } else if (p >= ML_P_THROTTLE) {
        ips[i].throttled = true;
        anyMitigation = true;

        Serial.printf("[INFO] IP=%s ML p=%.2f -> THROTTLE\n",
                      ipToString(ips[i].ip).c_str(), p);
      }
    }
  }

  if (!anyMitigation && failRateTotal < 0.5f) {
    updateEWMA(reqRateTotal, muReq, varReq);
    updateEWMA(failRateTotal, muFail, varFail);
    updateEWMA(nfRateTotal, muNF, varNF);
  }

  for (int i = 0; i < MAX_IPS; i++) {
    if (!ips[i].used) continue;
    ips[i].winReq = 0;
    ips[i].winFail = 0;
    ips[i].winNotFound = 0;
  }
}

int preHandleAndCount(int type) {
  IPAddress rip = server.client().remoteIP();
  uint32_t ip = ipToU32(rip);
  int idx = getIpSlot(ip);

  IpEntry &e = ips[idx];
  e.lastSeen = millis();

  if (isBlocked(e)) {
    server.send(403, "text/plain", "BLOCKED by IDS");
    return -1;
  }

  if (e.throttled) delay(THROTTLE_DELAY_MS);

  e.totalReq++;
  e.winReq++;

  if (type == 1) e.totalLogin++;
  if (type == 3) {
    e.totalNotFound++;
    e.winNotFound++;
  }

  return idx;
}

void handleRoot() {
  int idx = preHandleAndCount(0);
  if (idx < 0) return;
  server.send(200, "text/plain", "ESP32 IDS: OK");
}

void handleLogin() {
  int idx = preHandleAndCount(1);
  if (idx < 0) return;

  String token = server.arg("token");

  if (token == "1234") {
    server.send(200, "text/plain", "LOGIN OK");
  } else {
    ips[idx].totalFail++;
    ips[idx].winFail++;
    server.send(401, "text/plain", "LOGIN FAIL");
  }
}

void handleStatus() {
  int idx = preHandleAndCount(2);
  if (idx < 0) return;

  String msg;
  msg += "window_ms=" + String(WINDOW_MS) + "\n";
  msg += "muReq=" + String(muReq, 3) + "\n";
  msg += "muFail=" + String(muFail, 3) + "\n";
  msg += "muNF=" + String(muNF, 3) + "\n\n";

  msg += "IP TABLE (max " + String(MAX_IPS) + "):\n";
  for (int i = 0; i < MAX_IPS; i++) {
    if (!ips[i].used) continue;

    long msLeft = (long)(ips[i].blockedUntil - millis());
    if (msLeft < 0) msLeft = 0;

    msg += "ip=" + ipToString(ips[i].ip) + "\n";
    msg += "  totalReq=" + String(ips[i].totalReq) +
           " totalFail=" + String(ips[i].totalFail) +
           " total404=" + String(ips[i].totalNotFound) + "\n";
    msg += "  winReq=" + String(ips[i].winReq) +
           " winFail=" + String(ips[i].winFail) +
           " win404=" + String(ips[i].winNotFound) + "\n";
    msg += "  throttled=" + String(ips[i].throttled ? "true" : "false") + "\n";
    msg += "  blocked_ms_left=" + String(msLeft) +
           " offense=" + String(ips[i].offense) + "\n";
  }

  server.send(200, "text/plain", msg);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  server.on("/", handleRoot);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/status", HTTP_GET, handleStatus);

  server.onNotFound([]() {
    int idx = preHandleAndCount(3);
    if (idx < 0) return;
    server.send(404, "text/plain", "Not found");
  });

  server.begin();

  bootAt = millis();
  windowStart = millis();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - windowStart >= WINDOW_MS) {
    evaluateWindowAndMitigate();
    windowStart = now;
  }
}