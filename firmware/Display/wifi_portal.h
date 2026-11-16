// Minimal self-contained Wi-Fi provisioning: open AP + captive-portal form -> credentials saved in Preferences.
// No third-party library needed. The web UI (web_ui.h) runs on the same server afterwards.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "config.h"

namespace septa {

class WifiPortal {
 public:
  bool loadCredentials(String& ssid, String& pass) {
    Preferences p; p.begin("wifi", true); ssid = p.getString("ssid", ""); pass = p.getString("pass", ""); p.end();
    return ssid.length() > 0;
  }
  void saveCredentials(const String& ssid, const String& pass) {
    Preferences p; p.begin("wifi", false); p.putString("ssid", ssid); p.putString("pass", pass); p.end();
  }
  void clearCredentials() { Preferences p; p.begin("wifi", false); p.clear(); p.end(); }

  // Runs the captive portal until credentials are submitted, then reboots. Never returns.
  // timeoutMs > 0: reboot after that long without a submission (so a router that was briefly down doesn't
  // strand the board in setup mode forever).
  void runSetupBlocking(unsigned long timeoutMs = 0) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(SETUP_AP_NAME);
    IPAddress ip = WiFi.softAPIP();
    dns_.start(53, "*", ip);
    server_.onNotFound([this]() { handleRoot(); });
    server_.on("/", [this]() { handleRoot(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.begin();
    Serial.printf("[setup] AP '%s' up at %s\n", SETUP_AP_NAME, ip.toString().c_str());
    unsigned long t0 = millis();
    for (;;) {
      dns_.processNextRequest(); server_.handleClient(); delay(5);
      if (timeoutMs && millis() - t0 > timeoutMs) { Serial.println("[setup] timeout, retrying Wi-Fi"); ESP.restart(); }
    }
  }

  // After setup: the sketch registers the web UI on this server (see web_ui.h).
  WebServer& server() { return server_; }
  void handle() { server_.handleClient(); }

 private:
  WebServer server_{80};
  DNSServer dns_;

  void handleRoot() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); n = 0; }
    String opts;
    for (int i = 0; i < n; ++i) { opts += "<option>"; opts += WiFi.SSID(i); opts += "</option>"; }
    String page =
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>E-paper setup</title>"
      "<body style='font-family:sans-serif;max-width:24em;margin:2em auto;line-height:1.5'>"
      "<h2>E-paper display</h2><p>Connect this board to your home Wi-Fi.</p>"
      "<form method=post action=/save>"
      "<label>Network<br><input name=ssid list=nets required style='width:100%;font-size:1.1em'><datalist id=nets>" + opts + "</datalist></label><br><br>"
      "<label>Password<br><input name=pass type=password style='width:100%;font-size:1.1em'></label><br><br>"
      "<button style='font-size:1.1em;padding:.5em 1.2em'>Save and restart</button></form>"
      "<p style='color:#666'>If your network isn't listed yet, reload the page.</p></body>";
    server_.send(200, "text/html", page);
  }
  void handleSave() {
    String ssid = server_.arg("ssid"), pass = server_.arg("pass");
    if (!ssid.length()) { server_.send(400, "text/plain", "Network name is required."); return; }
    saveCredentials(ssid, pass);
    server_.send(200, "text/html", "<meta name=viewport content='width=device-width'><body style='font-family:sans-serif;margin:2em'>"
                 "<h2>Saved.</h2><p>The board is restarting and will join <b>" + ssid + "</b>. You can close this page.</p></body>");
    delay(800);
    ESP.restart();
  }
};

}  // namespace septa
