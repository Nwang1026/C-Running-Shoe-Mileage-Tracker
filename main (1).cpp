#include <Arduino.h>
#include <SPI.h>
#include <LovyanGFX.hpp>
#include "CrowPanelGFX.h"
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <WiFi.h>
#include <FS.h>
using fs::FS;
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

constexpr int TOUCH_CS = 0;
constexpr int TOUCH_IRQ = 36;
constexpr int TOUCH_SCK = 12;
constexpr int TOUCH_MISO = 13;
constexpr int TOUCH_MOSI = 11;
constexpr uint16_t SCREEN_W = 480;
constexpr uint16_t SCREEN_H = 272;
constexpr uint8_t SHOE_COUNT = 8;
constexpr uint32_t STRAVA_SYNC_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr char SETUP_AP_NAME[] = "Shoe-Dashboard";
constexpr char SETUP_AP_PASSWORD[] = "shoe-setup";

struct Shoe {
  String stravaId;
  String name;
  uint32_t miles;
  uint32_t replaceAt;
  uint16_t color;
};

Shoe shoes[SHOE_COUNT] = {
  {"", "Daily Trainer", 212, 400, TFT_CYAN},
  {"", "Trail Shoes", 356, 400, TFT_ORANGE},
  {"", "Race Shoes", 104, 250, TFT_MAGENTA},
  {"", "Recovery Pair", 48, 350, TFT_GREEN}
};
uint8_t shoeCount = 4;

CrowPanelGFX tft;
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
Preferences preferences;
WebServer server(80);
uint32_t lastStravaSyncMs = 0;
String stravaStatus = "Open the private setup page to connect Strava.";

bool detailView = false;
uint8_t selectedShoe = 0;
bool touchWasDown = false;
uint32_t lastTouchMs = 0;

uint8_t healthPercent(const Shoe &shoe) {
  if (shoe.replaceAt == 0 || shoe.miles >= shoe.replaceAt) return 0;
  return (uint32_t)(shoe.replaceAt - shoe.miles) * 100 / shoe.replaceAt;
}

uint16_t healthColor(const Shoe &shoe) {
  uint8_t health = healthPercent(shoe);
  if (health > 35) return TFT_GREEN;
  if (health > 10) return TFT_ORANGE;
  return TFT_RED;
}

void drawPixelShoe(int16_t x, int16_t y, uint16_t color) {
  // A small deliberately pixel-style shoe icon; replace with a custom image later.
  tft.fillRect(x + 2, y + 13, 35, 7, color);
  tft.fillRect(x + 8, y + 8, 20, 7, color);
  tft.fillRect(x + 14, y + 4, 10, 5, color);
  tft.fillRect(x + 0, y + 18, 42, 4, TFT_WHITE);
  tft.fillRect(x + 4, y + 18, 34, 2, TFT_DARKGREY);
  tft.drawFastHLine(x + 12, y + 11, 12, TFT_WHITE);
}

void drawHeader(const char *title) {
  tft.fillRect(0, 0, SCREEN_W, 30, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextFont(2);
  tft.setCursor(10, 8);
  tft.print(title);
  tft.setCursor(365, 8);
  tft.print("STRAVA");
  tft.setTextColor(TFT_ORANGE, TFT_NAVY);
  tft.print(" SETUP");
}

void drawDashboard() {
  detailView = false;
  tft.fillScreen(TFT_BLACK);
  drawHeader("SHOE MILEAGE");

  for (uint8_t i = 0; i < shoeCount && i < SHOE_COUNT; i++) {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    const int16_t x = 7 + col * 237;
    const int16_t y = 40 + row * 57;
    const Shoe &shoe = shoes[i];
    const uint8_t health = healthPercent(shoe);
    const uint16_t barColor = healthColor(shoe);

    tft.drawRoundRect(x, y, 229, 52, 5, TFT_DARKGREY);
    drawPixelShoe(x + 8, y + 10, shoe.color);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(x + 63, y + 6);
    String label = shoe.name.length() > 16 ? shoe.name.substring(0, 16) : shoe.name;
    tft.print(label);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(x + 63, y + 23);
    tft.printf("%u / %u mi", shoe.miles, shoe.replaceAt);

    tft.drawRoundRect(x + 63, y + 38, 156, 9, 3, TFT_DARKGREY);
    uint16_t filled = (uint32_t)152 * health / 100;
    if (filled > 0) tft.fillRoundRect(x + 65, y + 40, filled, 5, 2, barColor);
    tft.setTextColor(barColor, TFT_BLACK);
    tft.setCursor(x + 178, y + 7);
    if (health == 0) tft.print("REPLACE");
    else if (health <= 10) tft.print("SOON");
    else tft.printf("%u%%", health);
  }
}

void drawDetail(uint8_t index) {
  detailView = true;
  const Shoe &shoe = shoes[index];
  const uint8_t health = healthPercent(shoe);
  const uint16_t barColor = healthColor(shoe);

  tft.fillScreen(TFT_BLACK);
  drawHeader("SHOE DETAILS");
  drawPixelShoe(20, 49, shoe.color);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(75, 54);
  tft.print(shoe.name);

  tft.setTextFont(2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(20, 102);
  tft.printf("Distance: %u miles", shoe.miles);
  tft.setCursor(20, 124);
  tft.printf("Replacement point: %u miles", shoe.replaceAt);
  tft.setCursor(20, 146);
  if (health == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("Replace this pair now.");
  } else if (health <= 10) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.print("Replacement time is close.");
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.printf("%u%% life remaining", health);
  }

  tft.drawRoundRect(20, 172, 280, 22, 8, TFT_DARKGREY);
  uint16_t filled = (uint32_t)276 * health / 100;
  if (filled > 0) tft.fillRoundRect(22, 174, filled, 18, 6, barColor);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(75, 210);
  tft.print("Tap anywhere to go back");
}

void handleTap(int16_t x, int16_t y) {
  if (detailView) {
    drawDashboard();
    return;
  }
  if (y >= 40 && y < 268) {
    const uint8_t column = x >= 240 ? 1 : 0;
    const uint8_t row = (y - 40) / 57;
    const uint8_t index = row * 2 + column;
    if (index < shoeCount) {
      selectedShoe = index;
      drawDetail(selectedShoe);
    }
  }
}

String encodeForm(const String &input) {
  const char *hex = "0123456789ABCDEF";
  String output;
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') output += c;
    else { output += '%'; output += hex[(c >> 4) & 15]; output += hex[c & 15]; }
  }
  return output;
}

bool stravaTokenRequest(const String &body, JsonDocument &document) {
  WiFiClientSecure client;
  // The request is HTTPS encrypted. Use this only for a personal Strava app and revoke access if the board is lost.
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, "https://www.strava.com/oauth/token")) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int result = http.POST(body);
  if (result != HTTP_CODE_OK) { stravaStatus = "Strava token request failed (" + String(result) + ")."; http.end(); return false; }
  DeserializationError error = deserializeJson(document, http.getString());
  http.end();
  if (error) { stravaStatus = "Strava sent unreadable authorization data."; return false; }
  return true;
}

bool refreshStravaToken() {
  String clientId = preferences.getString("stravaId", "");
  String clientSecret = preferences.getString("stravaSecret", "");
  String refreshToken = preferences.getString("refresh", "");
  if (clientId.isEmpty() || clientSecret.isEmpty() || refreshToken.isEmpty()) return false;
  JsonDocument response;
  String body = "client_id=" + encodeForm(clientId) + "&client_secret=" + encodeForm(clientSecret) + "&grant_type=refresh_token&refresh_token=" + encodeForm(refreshToken);
  if (!stravaTokenRequest(body, response)) return false;
  String access = response["access_token"] | "";
  String refreshed = response["refresh_token"] | "";
  if (access.isEmpty() || refreshed.isEmpty()) return false;
  preferences.putString("access", access);
  preferences.putString("refresh", refreshed);
  return true;
}

bool syncStravaShoes() {
  if (WiFi.status() != WL_CONNECTED) { stravaStatus = "Waiting for Wi-Fi."; return false; }
  if (!refreshStravaToken()) { stravaStatus = "Connect Strava from the setup page."; return false; }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, "https://www.strava.com/api/v3/athlete/gear")) return false;
  http.addHeader("Authorization", "Bearer " + preferences.getString("access", ""));
  int result = http.GET();
  if (result != HTTP_CODE_OK) { stravaStatus = "Strava shoe request failed (" + String(result) + ")."; http.end(); return false; }
  JsonDocument response;
  DeserializationError error = deserializeJson(response, http.getString());
  http.end();
  if (error || !response.is<JsonArray>()) { stravaStatus = "Strava sent unreadable shoe data."; return false; }
  uint8_t found = 0;
  for (JsonObject gear : response.as<JsonArray>()) {
    if (String(gear["gear_type"] | "") != "shoes" || found >= SHOE_COUNT) continue;
    String id = gear["id"] | "";
    shoes[found].stravaId = id;
    shoes[found].name = gear["name"] | "Unnamed shoe";
    shoes[found].miles = static_cast<uint32_t>((gear["distance"] | 0.0f) / 1609.344f + 0.5f);
    shoes[found].replaceAt = preferences.getUInt(("limit" + id).c_str(), 400);
    shoes[found].color = (uint16_t[]){TFT_CYAN, TFT_ORANGE, TFT_MAGENTA, TFT_GREEN, TFT_YELLOW, TFT_BLUE, TFT_PINK, TFT_SKYBLUE}[found];
    found++;
  }
  shoeCount = found;
  preferences.putUChar("shoeCount", shoeCount);
  for (uint8_t i = 0; i < shoeCount; ++i) {
    String key = "m" + String(i);
    preferences.putUInt(key.c_str(), shoes[i].miles);
  }
  lastStravaSyncMs = millis();
  stravaStatus = found ? "Strava shoe mileage is up to date." : "No shoes were returned by Strava.";
  drawDashboard();
  return found > 0;
}

void showSetupPage() {
  String clientId = preferences.getString("stravaId", "");
  String authLink;
  if (!clientId.isEmpty()) {
    authLink = "<p><a href='https://www.strava.com/oauth/authorize?client_id=" + encodeForm(clientId) + "&response_type=code&redirect_uri=http%3A%2F%2Flocalhost&approval_prompt=force&scope=read,activity:read_all'>1. Authorize with Strava</a></p><p>When the browser reaches localhost, copy the code value from its address bar and paste it below.</p>";
  }
  String page = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><style>body{font:16px sans-serif;max-width:620px;margin:auto;padding:18px;background:#111;color:#eee}input{box-sizing:border-box;width:100%;padding:9px;margin:4px 0 12px}button,a{display:inline-block;padding:10px;background:#fc4c02;border:0;border-radius:5px;color:white;text-decoration:none}section{border:1px solid #555;padding:12px;margin:14px 0}</style><h1>Shoe dashboard</h1><p>" + stravaStatus + "</p><section><h2>Wi-Fi</h2><form method=post action='/wifi'>Network name<input name=ssid value='" + preferences.getString("ssid", "") + "'>Password<input type=password name=pass><button>Save Wi-Fi and restart</button></form></section><section><h2>Strava</h2><p>Create a personal Strava API app with callback domain <b>localhost</b>. Keep its client secret private.</p><form method=post action='/app'>Client ID<input name=id value='" + clientId + "'>Client secret<input type=password name=secret><button>Save Strava app</button></form>" + authLink + "<form method=post action='/code'>Authorization code<input name=code><button>2. Connect Strava</button></form><p><a href='/sync'>Sync shoes now</a></p></section>";
  server.send(200, "text/html", page);
}

void startSetupServer() {
  server.on("/", HTTP_GET, showSetupPage);
  server.on("/wifi", HTTP_POST, []() { preferences.putString("ssid", server.arg("ssid")); preferences.putString("pass", server.arg("pass")); server.send(200, "text/plain", "Saved. The dashboard will restart now."); delay(300); ESP.restart(); });
  server.on("/app", HTTP_POST, []() { preferences.putString("stravaId", server.arg("id")); if (!server.arg("secret").isEmpty()) preferences.putString("stravaSecret", server.arg("secret")); stravaStatus = "Strava app details saved."; server.sendHeader("Location", "/"); server.send(303); });
  server.on("/code", HTTP_POST, []() {
    JsonDocument response;
    String body = "client_id=" + encodeForm(preferences.getString("stravaId", "")) + "&client_secret=" + encodeForm(preferences.getString("stravaSecret", "")) + "&code=" + encodeForm(server.arg("code")) + "&grant_type=authorization_code";
    if (stravaTokenRequest(body, response)) { preferences.putString("access", String(response["access_token"] | "")); preferences.putString("refresh", String(response["refresh_token"] | "")); stravaStatus = "Strava connected."; syncStravaShoes(); }
    server.sendHeader("Location", "/"); server.send(303);
  });
  server.on("/sync", HTTP_GET, []() { syncStravaShoes(); server.sendHeader("Location", "/"); server.send(303); });
  server.begin();
}

void connectNetwork() {
  String ssid = preferences.getString("ssid", "");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASSWORD);
  if (!ssid.isEmpty()) {
    WiFi.begin(ssid.c_str(), preferences.getString("pass", "").c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) stravaStatus = "Connected to Wi-Fi. Setup page is still available at 192.168.4.1.";
  else stravaStatus = "Join Shoe-Dashboard, then open 192.168.4.1.";
}

void setup() {
  preferences.begin("shoeMiles", false);
  shoeCount = min<uint8_t>(preferences.getUChar("shoeCount", shoeCount), SHOE_COUNT);
  for (uint8_t i = 0; i < shoeCount; i++) {
    String key = "m" + String(i);
    shoes[i].miles = preferences.getUInt(key.c_str(), shoes[i].miles);
  }

  tft.init();
  tft.setRotation(0);
  tft.setBrightness(180);
  tft.fillScreen(TFT_BLACK);

  // The built-in touchscreen uses SPI2; the RGB display has its own parallel bus.
  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin();
  touch.setRotation(0);

  drawDashboard();
  connectNetwork();
  startSetupServer();
  if (WiFi.status() == WL_CONNECTED && !preferences.getString("refresh", "").isEmpty()) syncStravaShoes();
  drawDashboard();
}

void loop() {
  server.handleClient();
  bool touched = touch.touched();
  if (touched && !touchWasDown && millis() - lastTouchMs > 180) {
    TS_Point point = touch.getPoint();
    // The resistive panel reports raw values; map them to the 480×272 screen.
    int16_t screenX = constrain(map(point.x, 100, 4000, 0, SCREEN_W - 1), 0, SCREEN_W - 1);
    int16_t screenY = constrain(map(point.y, 100, 4000, 0, SCREEN_H - 1), 0, SCREEN_H - 1);
    handleTap(screenX, screenY);
    lastTouchMs = millis();
  }
    touchWasDown = touched;
  if (WiFi.status() == WL_CONNECTED && !preferences.getString("refresh", "").isEmpty() && millis() - lastStravaSyncMs >= STRAVA_SYNC_MS) syncStravaShoes();
  delay(15);
}
