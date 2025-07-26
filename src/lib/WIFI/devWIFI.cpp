#include "device.h"

#include "deferred.h"

#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <soc/uart_pins.h>
#include <DNSServer.h>

#include <set>
#include <StreamString.h>

#include "ArduinoJson.h"
#include "AsyncJson.h"
#include <ESPAsyncWebServer.h>

#include "common.h"
#include "POWERMGNT.h"
#include "FHSS.h"
#include "hwTimer.h"
#include "options.h"
#include "helpers.h"
#include "devButton.h"
#include "WebContent.h"

#include "config.h"

extern TxConfig config;
extern void setButtonColors(uint8_t b1, uint8_t b2);

extern unsigned long rebootTime;

static char station_ssid[33];
static char station_password[65];

static bool wifiStarted = false;
bool webserverPreventAutoStart = false;

static wl_status_t laststatus = WL_IDLE_STATUS;
volatile WiFiMode_t wifiMode = WIFI_OFF;
static volatile WiFiMode_t changeMode = WIFI_OFF;
static volatile unsigned long changeTime = 0;

static const byte DNS_PORT = 53;
static IPAddress netMsk(255, 255, 255, 0);
static DNSServer dnsServer;
static IPAddress ipAddress;
static AsyncWebServer server(80);
static bool servicesStarted = false;
static constexpr uint32_t STALE_WIFI_SCAN = 20000;
static uint32_t lastScanTimeMS = 0;

static bool target_seen = false;
static uint8_t target_pos = 0;
static String target_found;
static bool target_complete = false;
static bool force_update = false;
static uint32_t totalSize;

void setWifiUpdateMode()
{
  // No need to ExitBindingMode(), the radio will be stopped stopped when start the Wifi service.
  // Need to change this before the mode change event so the LED is updated
  InBindingMode = false;
  setConnectionState(wifiUpdate);
}

/** Is this an IP? */
static boolean isIp(String str)
{
  for (size_t i = 0; i < str.length(); i++)
  {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9'))
    {
      return false;
    }
  }
  return true;
}

/** IP to String? */
static String toStringIp(IPAddress ip)
{
  String res = "";
  for (int i = 0; i < 3; i++)
  {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

static bool captivePortal(AsyncWebServerRequest *request)
{
  extern const char *wifi_hostname;

  if (!isIp(request->host()) && request->host() != (String(wifi_hostname) + ".local"))
  {
    request->redirect(String("http://") + toStringIp(request->client()->localIP()));
    return true;
  }
  return false;
}

static struct {
  const char *url;
  const char *contentType;
  const uint8_t* content;
  const size_t size;
} files[] = {
  {"/scan.js", "text/javascript", (uint8_t *)SCAN_JS, sizeof(SCAN_JS)},
  {"/mui.js", "text/javascript", (uint8_t *)MUI_JS, sizeof(MUI_JS)},
  {"/elrs.css", "text/css", (uint8_t *)ELRS_CSS, sizeof(ELRS_CSS)},
  {"/hardware.html", "text/html", (uint8_t *)HARDWARE_HTML, sizeof(HARDWARE_HTML)},
  {"/hardware.js", "text/javascript", (uint8_t *)HARDWARE_JS, sizeof(HARDWARE_JS)},
  {"/cw.html", "text/html", (uint8_t *)CW_HTML, sizeof(CW_HTML)},
  {"/cw.js", "text/javascript", (uint8_t *)CW_JS, sizeof(CW_JS)},
};

static void WebUpdateSendContent(AsyncWebServerRequest *request)
{
  for (size_t i=0 ; i<ARRAY_SIZE(files) ; i++) {
    if (request->url().equals(files[i].url)) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, files[i].contentType, files[i].content, files[i].size);
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
  }
  request->send(404, "text/plain", "File not found");
}

static void WebUpdateHandleRoot(AsyncWebServerRequest *request)
{
  if (captivePortal(request))
  { // If captive portal redirect instead of displaying the page.
    return;
  }
  force_update = request->hasArg("force");
  AsyncWebServerResponse *response;
  if (connectionState == hardwareUndefined)
  {
    response = request->beginResponse_P(200, "text/html", (uint8_t*)HARDWARE_HTML, sizeof(HARDWARE_HTML));
  }
  else
  {
    response = request->beginResponse_P(200, "text/html", (uint8_t*)INDEX_HTML, sizeof(INDEX_HTML));
  }
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

static void putFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  static File file;
  static size_t bytes;
  if (!file || request->url() != file.name()) {
    file = SPIFFS.open(request->url(), "w");
    bytes = 0;
  }
  file.write(data, len);
  bytes += len;
  if (bytes == total) {
    file.close();
  }
}

static void getFile(AsyncWebServerRequest *request)
{
  if (request->url() == "/options.json") {
    request->send(200, "application/json", getOptions());
  } else if (request->url() == "/hardware.json") {
    request->send(200, "application/json", getHardware());
  } else {
    request->send(SPIFFS, request->url().c_str(), "text/plain", true);
  }
}

static void HandleReboot(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "Kill -9, no more CPU time!");
  response->addHeader("Connection", "close");
  request->send(response);
  request->client()->close();
  rebootTime = millis() + 100;
}

static void HandleReset(AsyncWebServerRequest *request)
{
  if (request->hasArg("hardware")) {
    SPIFFS.remove("/hardware.json");
  }
  if (request->hasArg("options")) {
    SPIFFS.remove("/options.json");
  }
  if (request->hasArg("model") || request->hasArg("config")) {
    config.SetDefaults(true);
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "Reset complete, rebooting...");
  response->addHeader("Connection", "close");
  request->send(response);
  request->client()->close();
  rebootTime = millis() + 100;
}

static void UpdateSettings(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (firmwareOptions.flash_discriminator != json["flash-discriminator"].as<uint32_t>()) {
    request->send(409, "text/plain", "Mismatched device identifier, refresh the page and try again.");
    return;
  }

  File file = SPIFFS.open("/options.json", "w");
  serializeJson(json, file);
  request->send(200);
}

static const char *GetConfigUidType(const JsonObject json)
{
  if (firmwareOptions.hasUID)
  {
    if (json["options"]["customised"] | false)
      return "Overridden";
    else
      return "Flashed";
  }
  return "Not set (using MAC address)";
}

static void GetConfiguration(AsyncWebServerRequest *request)
{
  bool exportMode = request->hasArg("export");
  AsyncJsonResponse *response = new AsyncJsonResponse();
  JsonObject json = response->getRoot();

  if (!exportMode)
  {
    JsonDocument options;
    deserializeJson(options, getOptions());
    json["options"] = options;
  }

  JsonArray uid = json["config"]["uid"].to<JsonArray>();
  copyArray(UID, UID_LEN, uid);

  int button_count = 0;
  if (GPIO_PIN_BUTTON != UNDEF_PIN)
    button_count = 1;
  if (GPIO_PIN_BUTTON2 != UNDEF_PIN)
    button_count = 2;
  for (int button=0 ; button<button_count ; button++)
  {
    const tx_button_color_t *buttonColor = config.GetButtonActions(button);
    if (hardware_int(button == 0 ? HARDWARE_button_led_index : HARDWARE_button2_led_index) != -1) {
      json["config"]["button-actions"][button]["color"] = buttonColor->val.color;
    }
    for (int pos=0 ; pos<button_GetActionCnt() ; pos++)
    {
      json["config"]["button-actions"][button]["action"][pos]["is-long-press"] = buttonColor->val.actions[pos].pressType ? true : false;
      json["config"]["button-actions"][button]["action"][pos]["count"] = buttonColor->val.actions[pos].count;
      json["config"]["button-actions"][button]["action"][pos]["action"] = buttonColor->val.actions[pos].action;
    }
  }
  if (exportMode)
  {
    json["config"]["fan-mode"] = config.GetFanMode();
    json["config"]["power-fan-threshold"] = config.GetPowerFanThreshold();

    for (int model = 0 ; model < CONFIG_TX_MODEL_CNT ; model++)
    {
      const model_config_t &modelConfig = config.GetModelConfig(model);
      String strModel(model);
      JsonObject modelJson = json["config"]["model"][strModel].to<JsonObject>();
      modelJson["packet-rate"] = modelConfig.rate;
      modelJson["telemetry-ratio"] = modelConfig.tlm;
      modelJson["switch-mode"] = modelConfig.switchMode;
      modelJson["power"]["max-power"] = modelConfig.power;
      modelJson["power"]["dynamic-power"] = modelConfig.dynamicPower;
      modelJson["power"]["boost-channel"] = modelConfig.boostChannel;
      modelJson["model-match"] = modelConfig.modelMatch;
    }
  }

  if (!exportMode)
  {
    json["config"]["ssid"] = station_ssid;
    json["config"]["mode"] = wifiMode == WIFI_STA ? "STA" : "AP";
    json["config"]["product_name"] = product_name;
    json["config"]["lua_name"] = device_name;
    json["config"]["reg_domain"] = FHSSgetRegulatoryDomain();
    json["config"]["uidtype"] = GetConfigUidType(json);
  }

  response->setLength();
  request->send(response);
}

static void UpdateConfiguration(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (json.containsKey("button-actions")) {
    const JsonArray &array = json["button-actions"].as<JsonArray>();
    for (size_t button=0 ; button<array.size() ; button++)
    {
      tx_button_color_t action;
      for (int pos=0 ; pos<button_GetActionCnt() ; pos++)
      {
        action.val.actions[pos].pressType = array[button]["action"][pos]["is-long-press"];
        action.val.actions[pos].count = array[button]["action"][pos]["count"];
        action.val.actions[pos].action = array[button]["action"][pos]["action"];
      }
      action.val.color = array[button]["color"];
      config.SetButtonActions(button, &action);
    }
  }
  config.Commit();
  request->send(200, "text/plain", "Import/update complete");
}

static void ImportConfiguration(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (json.containsKey("config"))
  {
    json = json["config"];
  }

  if (json.containsKey("fan-mode")) config.SetFanMode(json["fan-mode"]);
  if (json.containsKey("power-fan-threshold")) config.SetPowerFanThreshold(json["power-fan-threshold"]);
  if (json.containsKey("model"))
  {
    for(JsonPair kv : json["model"].as<JsonObject>())
    {
      uint8_t model = atoi(kv.key().c_str());
      JsonObject modelJson = kv.value();

      config.SetModelId(model);
      if (modelJson.containsKey("packet-rate")) config.SetRate(modelJson["packet-rate"]);
      if (modelJson.containsKey("telemetry-ratio")) config.SetTlm(modelJson["telemetry-ratio"]);
      if (modelJson.containsKey("switch-mode")) config.SetSwitchMode(modelJson["switch-mode"]);
      if (modelJson.containsKey("power"))
      {
        if (modelJson["power"].containsKey("max-power")) config.SetPower(modelJson["power"]["max-power"]);
        if (modelJson["power"].containsKey("dynamic-power")) config.SetDynamicPower(modelJson["power"]["dynamic-power"]);
        if (modelJson["power"].containsKey("boost-channel")) config.SetBoostChannel(modelJson["power"]["boost-channel"]);
      }
      if (modelJson.containsKey("model-match")) config.SetModelMatch(modelJson["model-match"]);
      // have to commmit after each model is updated
      config.Commit();
    }
  }

  UpdateConfiguration(request, json);
}

static void WebUpdateButtonColors(AsyncWebServerRequest *request, JsonVariant &json)
{
  int button1Color = json[0].as<int>();
  int button2Color = json[1].as<int>();
  setButtonColors(button1Color, button2Color);
  request->send(200);
}

static void WebUpdateGetTarget(AsyncWebServerRequest *request)
{
  JsonDocument json;
  json["target"] = &target_name[4];
  json["version"] = VERSION;
  json["product_name"] = product_name;
  json["lua_name"] = device_name;
  json["reg_domain"] = FHSSgetRegulatoryDomain();
  json["git-commit"] = commit;
  json["module-type"] = "TX";
#if defined(RADIO_SX128X)
  json["radio-type"] = "SX128X";
  json["has-sub-ghz"] = false;
#endif

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  serializeJson(json, *response);
  request->send(response);
}

static void WebUpdateSendNetworks(AsyncWebServerRequest *request)
{
  int numNetworks = WiFi.scanComplete();
  if (numNetworks >= 0 && millis() - lastScanTimeMS < STALE_WIFI_SCAN) {
    std::set<String> vs;
    String s="[";
    for(int i=0 ; i<numNetworks ; i++) {
      String w = WiFi.SSID(i);
      if (vs.find(w)==vs.end() && w.length()>0) {
        if (!vs.empty()) s += ",";
        s += "\"" + w + "\"";
        vs.insert(w);
      }
    }
    s+="]";
    request->send(200, "application/json", s);
  } else {
    if (WiFi.scanComplete() != WIFI_SCAN_RUNNING)
    {
      WiFi.scanNetworks(true);
      lastScanTimeMS = millis();
    }
    request->send(204, "application/json", "[]");
  }
}

static void sendResponse(AsyncWebServerRequest *request, const String &msg, WiFiMode_t mode) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", msg);
  response->addHeader("Connection", "close");
  request->send(response);
  request->client()->close();
  changeTime = millis();
  changeMode = mode;
}

static void WebUpdateAccessPoint(AsyncWebServerRequest *request)
{
  String msg = String("Access Point starting, please connect to access point '") + wifi_ap_ssid + "' with password '" + wifi_ap_password + "'";
  sendResponse(request, msg, WIFI_AP);
}

static void WebUpdateConnect(AsyncWebServerRequest *request)
{
  String msg = String("Connecting to network '") + station_ssid + "', connect to http://" +
    wifi_hostname + ".local from a browser on that network";
  sendResponse(request, msg, WIFI_STA);
}

static void WebUpdateSetHome(AsyncWebServerRequest *request)
{
  String ssid = request->arg("network");
  String password = request->arg("password");

  strcpy(station_ssid, ssid.c_str());
  strcpy(station_password, password.c_str());
  if (request->hasArg("save")) {
    strlcpy(firmwareOptions.home_wifi_ssid, ssid.c_str(), sizeof(firmwareOptions.home_wifi_ssid));
    strlcpy(firmwareOptions.home_wifi_password, password.c_str(), sizeof(firmwareOptions.home_wifi_password));
    saveOptions();
  }
  WebUpdateConnect(request);
}

static void WebUpdateForget(AsyncWebServerRequest *request)
{
  firmwareOptions.home_wifi_ssid[0] = 0;
  firmwareOptions.home_wifi_password[0] = 0;
  saveOptions();
  station_ssid[0] = 0;
  station_password[0] = 0;
  String msg = String("Home network forgotten, please connect to access point '") + wifi_ap_ssid + "' with password '" + wifi_ap_password + "'";
  sendResponse(request, msg, WIFI_AP);
}

static void WebUpdateHandleNotFound(AsyncWebServerRequest *request)
{
  if (captivePortal(request))
  { // If captive portal redirect instead of displaying the error page.
    return;
  }
  String message = F("File Not Found\n\n");
  message += F("URI: ");
  message += request->url();
  message += F("\nMethod: ");
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += request->args();
  message += F("\n");

  for (uint8_t i = 0; i < request->args(); i++)
  {
    message += String(F(" ")) + request->argName(i) + F(": ") + request->arg(i) + F("\n");
  }
  AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", message);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

static void corsPreflightResponse(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(204, "text/plain");
  request->send(response);
}

static void WebUploadResponseHandler(AsyncWebServerRequest *request) {
  if (target_seen || Update.hasError()) {
    String msg;
    if (!Update.hasError() && Update.end()) {
      msg = String("{\"status\": \"ok\", \"msg\": \"Update complete. ");
      msg += "Please wait for a few seconds while the device reboots.\"}";
      rebootTime = millis() + 200;
    } else {
      StreamString p = StreamString();
      if (Update.hasError()) {
        Update.printError(p);
      } else {
        p.println("Not enough data uploaded!");
      }
      p.trim();
      msg = String("{\"status\": \"error\", \"msg\": \"") + p + "\"}";
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", msg);
    response->addHeader("Connection", "close");
    request->send(response);
    request->client()->close();
  } else {
    String message = String("{\"status\": \"mismatch\", \"msg\": \"<b>Current target:</b> ") + (const char *)&target_name[4] + ".<br>";
    if (target_found.length() != 0) {
      message += "<b>Uploaded image:</b> " + target_found + ".<br/>";
    }
    message += "<br/>It looks like you are flashing firmware with a different name to the current  firmware.  This sometimes happens because the hardware was flashed from the factory with an early version that has a different name. Or it may have even changed between major releases.";
    message += "<br/><br/>Please double check you are uploading the correct target, then proceed with 'Flash Anyway'.\"}";
    request->send(200, "application/json", message);
  }
}

static void WebUploadDataHandler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  force_update = force_update || request->hasArg("force");
  if (index == 0) {
    
    size_t filesize = request->header("X-FileSize").toInt();
    Update.begin(filesize, U_FLASH); // pass the size provided
    target_seen = false;
    target_found.clear();
    target_complete = false;
    target_pos = 0;
    totalSize = 0;
  }
  if (len) {
    if (Update.write(data, len) == len) {
      if (force_update || (totalSize == 0 && *data == 0x1F))
        target_seen = true;
      if (!target_seen) {
        for (size_t i=0 ; i<len ;i++) {
          if (!target_complete && (target_pos >= 4 || target_found.length() > 0)) {
            if (target_pos == 4) {
              target_found.clear();
            }
            if (data[i] == 0 || target_found.length() > 50) {
              target_complete = true;
            }
            else {
              target_found += (char)data[i];
            }
          }
          if (data[i] == target_name[target_pos]) {
            ++target_pos;
            if (target_pos >= target_name_size) {
              target_seen = true;
            }
          }
          else {
            target_pos = 0; // Startover
          }
        }
      }
      totalSize += len;
    }
  }
}

static void WebUploadForceUpdateHandler(AsyncWebServerRequest *request) {
  target_seen = true;
  if (request->arg("action").equals("confirm")) {
    WebUploadResponseHandler(request);
  } else {
    Update.abort();
    request->send(200, "application/json", "{\"status\": \"ok\", \"msg\": \"Update cancelled\"}");
  }
}

static size_t firmwareOffset = 0;
static size_t getFirmwareChunk(uint8_t *data, size_t len, size_t pos)
{
  uint8_t *dst;
  uint8_t alignedBuffer[7];
  if ((uintptr_t)data % 4 != 0)
  {
    // If data is not aligned, read aligned byes using the local buffer and hope the next call will be aligned
    dst = (uint8_t *)((uint32_t)alignedBuffer / 4 * 4);
    len = 4;
  }
  else
  {
    // Otherwise just make sure len is a multiple of 4 and smaller than a sector
    dst = data;
    len = constrain((len / 4) * 4, 4, SPI_FLASH_SEC_SIZE);
  }

  ESP.flashRead(firmwareOffset + pos, (uint32_t *)dst, len);

  // If using local stack buffer, move the 4 bytes into the passed buffer
  // data is known to not be aligned so it is moved byte-by-byte instead of as uint32_t*
  if ((void *)dst != (void *)data)
  {
    for (unsigned b=len; b>0; --b)
      *data++ = *dst++;
  }
  return len;
}

static void WebUpdateGetFirmware(AsyncWebServerRequest *request) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) {
      firmwareOffset = running->address;
  }
  const size_t firmwareTrailerSize = 4096;  // max number of bytes for the options/hardware layout json
  AsyncWebServerResponse *response = request->beginResponse("application/octet-stream", (size_t)ESP.getSketchSize() + firmwareTrailerSize, &getFirmwareChunk);
  String filename = String("attachment; filename=\"") + (const char *)&target_name[4] + "_" + VERSION + ".bin\"";
  response->addHeader("Content-Disposition", filename);
  request->send(response);
}

static void HandleContinuousWave(AsyncWebServerRequest *request) {
  if (request->hasArg("radio")) {
    SX12XX_Radio_Number_t radio = request->arg("radio").toInt() == 1 ? SX12XX_Radio_1 : SX12XX_Radio_2;

    AsyncWebServerResponse *response = request->beginResponse(204);
    response->addHeader("Connection", "close");
    request->send(response);
    request->client()->close();

    Radio.TXdoneCallback = [](){};
    Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq());

    POWERMGNT::init();
    POWERMGNT::setPower(POWERMGNT::getMinPower());

    Radio.startCWTest(FHSSconfig->freq_center, radio);
  } else {
    int radios = (GPIO_PIN_NSS_2 == UNDEF_PIN) ? 1 : 2;
    request->send(200, "application/json", String("{\"radios\": ") + radios + ", \"center\": "+ FHSSconfig->freq_center + "}");
  }
}

static bool initialize()
{
  wifiStarted = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  registerButtonFunction(ACTION_START_WIFI, [](){
    setWifiUpdateMode();
  });
  return true;
}

static void startWiFi(unsigned long now)
{
  if (wifiStarted) {
    return;
  }

  if (connectionState < FAILURE_STATES) {
    hwTimer::stop();

    // Set transmit power to minimum
    POWERMGNT::setPower(MinPower);

    setWifiUpdateMode();
    Radio.End();
  }

  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  strcpy(station_ssid, firmwareOptions.home_wifi_ssid);
  strcpy(station_password, firmwareOptions.home_wifi_password);
  if (station_ssid[0] == 0) {
    changeTime = now;
    changeMode = WIFI_AP;
  }
  else {
    changeTime = now;
    changeMode = WIFI_STA;
  }
  laststatus = WL_DISCONNECTED;
  wifiStarted = true;
}

static void startMDNS()
{
  if (!MDNS.begin(wifi_hostname))
  {
    return;
  }

  String options = "-DAUTO_WIFI_ON_INTERVAL=" + (firmwareOptions.wifi_auto_on_interval == -1 ? "-1" : String(firmwareOptions.wifi_auto_on_interval / 1000));

  if (firmwareOptions.unlock_higher_power)
  {
    options += " -DUNLOCK_HIGHER_POWER";
  }
  options += " -DTLM_REPORT_INTERVAL_MS=" + String(firmwareOptions.tlm_report_interval);
  options += " -DFAN_MIN_RUNTIME=" + String(firmwareOptions.fan_min_runtime);

  String instance = String(wifi_hostname) + "_" + WiFi.macAddress();
  instance.replace(":", "");
    MDNS.setInstanceName(instance);
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "vendor", "elrs");
    MDNS.addServiceTxt("http", "tcp", "target", (const char *)&target_name[4]);
    MDNS.addServiceTxt("http", "tcp", "device", (const char *)device_name);
    MDNS.addServiceTxt("http", "tcp", "product", (const char *)product_name);
    MDNS.addServiceTxt("http", "tcp", "version", VERSION);
    MDNS.addServiceTxt("http", "tcp", "options", options.c_str());
    MDNS.addServiceTxt("http", "tcp", "type", "tx");
}

static void addCaptivePortalHandlers()
{
    // windows 11 captive portal workaround
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request) { request->redirect("http://logout.net"); });
    // A 404 stops win 10 keep calling this repeatedly and panicking the esp32
    server.on("/wpad.dat", [](AsyncWebServerRequest *request) { request->send(404); });

    server.on("/generate_204", WebUpdateHandleRoot); // Android
    server.on("/gen_204", WebUpdateHandleRoot); // Android
    server.on("/library/test/success.html", WebUpdateHandleRoot); // apple call home
    server.on("/hotspot-detect.html", WebUpdateHandleRoot); // apple call home
    server.on("/connectivity-check.html", WebUpdateHandleRoot); // ubuntu
    server.on("/check_network_status.txt", WebUpdateHandleRoot); // ubuntu
    server.on("/ncsi.txt", WebUpdateHandleRoot); // windows call home
    server.on("/canonical.html", WebUpdateHandleRoot); // firefox captive portal call home
    server.on("/fwlink", WebUpdateHandleRoot);
    server.on("/redirect", WebUpdateHandleRoot); // microsoft redirect
    server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); }); // firefox captive portal call home
}

static void startServices()
{
  if (servicesStarted) {
    MDNS.end();
    startMDNS();
    return;
  }

  server.on("/", WebUpdateHandleRoot);
  server.on("/elrs.css", WebUpdateSendContent);
  server.on("/mui.js", WebUpdateSendContent);
  server.on("/scan.js", WebUpdateSendContent);
  server.on("/networks.json", WebUpdateSendNetworks);
  server.on("/sethome", WebUpdateSetHome);
  server.on("/forget", WebUpdateForget);
  server.on("/connect", WebUpdateConnect);
  server.on("/config", HTTP_GET, GetConfiguration);
  server.on("/access", WebUpdateAccessPoint);
  server.on("/target", WebUpdateGetTarget);
  server.on("/firmware.bin", WebUpdateGetFirmware);

  server.on("/update", HTTP_POST, WebUploadResponseHandler, WebUploadDataHandler);
  server.on("/update", HTTP_OPTIONS, corsPreflightResponse);
  server.on("/forceupdate", WebUploadForceUpdateHandler);
  server.on("/forceupdate", HTTP_OPTIONS, corsPreflightResponse);
  server.on("/cw.html", WebUpdateSendContent);
  server.on("/cw.js", WebUpdateSendContent);
  server.on("/cw", HandleContinuousWave);

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Max-Age", "600");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  server.on("/hardware.html", WebUpdateSendContent);
  server.on("/hardware.js", WebUpdateSendContent);
  server.on("/hardware.json", getFile).onBody(putFile);
  server.on("/options.json", HTTP_GET, getFile);
  server.on("/reboot", HandleReboot);
  server.on("/reset", HandleReset);

  server.addHandler(new AsyncCallbackJsonWebHandler("/config", UpdateConfiguration));
  server.addHandler(new AsyncCallbackJsonWebHandler("/options.json", UpdateSettings));
  server.addHandler(new AsyncCallbackJsonWebHandler("/buttons", WebUpdateButtonColors));
  server.addHandler(new AsyncCallbackJsonWebHandler("/import", ImportConfiguration, 32768U));

  addCaptivePortalHandlers();

  server.onNotFound(WebUpdateHandleNotFound);

  server.begin();

  dnsServer.start(DNS_PORT, "*", ipAddress);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);

  startMDNS();

  servicesStarted = true;
}

static void HandleWebUpdate()
{
  unsigned long now = millis();
  wl_status_t status = WiFi.status();

  if (status != laststatus && wifiMode == WIFI_STA) {
    switch(status) {
      case WL_NO_SSID_AVAIL:
      case WL_CONNECT_FAILED:
      case WL_CONNECTION_LOST:
        changeTime = now;
        changeMode = WIFI_AP;
        break;
      case WL_DISCONNECTED: // try reconnection
        changeTime = now;
        break;
      default:
        break;
    }
    laststatus = status;
  }
  if (status != WL_CONNECTED && wifiMode == WIFI_STA && (now - changeTime) > 30000) {
    changeTime = now;
    changeMode = WIFI_AP;
  }
  if (changeMode != wifiMode && changeMode != WIFI_OFF && (now - changeTime) > 500) {
    switch(changeMode) {
      case WIFI_AP:
        WiFi.disconnect();
        wifiMode = WIFI_AP;
        WiFi.setHostname(wifi_hostname); // hostname must be set before the mode is set to STA
        WiFi.mode(wifiMode);
        changeTime = now;
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        WiFi.softAPConfig(ipAddress, ipAddress, netMsk);
        WiFi.softAP(wifi_ap_ssid, wifi_ap_password);
        startServices();
        break;
      case WIFI_STA:
        wifiMode = WIFI_STA;
        WiFi.setHostname(wifi_hostname); // hostname must be set before the mode is set to STA
        WiFi.mode(wifiMode);
        changeTime = now;
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.begin(station_ssid, station_password);
        startServices();
      default:
        break;
    }
    changeMode = WIFI_OFF;
  }

  if (servicesStarted)
  {
    dnsServer.processNextRequest();
  }
}

static int start()
{
  ipAddress.fromString(wifi_ap_address);
  return firmwareOptions.wifi_auto_on_interval;
}

static int event()
{
  if (connectionState == wifiUpdate || connectionState > FAILURE_STATES)
  {
    if (!wifiStarted) {
      startWiFi(millis());
      return DURATION_IMMEDIATELY;
    }
  }
  else if (wifiStarted)
  {
    wifiStarted = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  return DURATION_IGNORE;
}

static int timeout()
{
  if (wifiStarted)
  {
    HandleWebUpdate();
    // All the web traffic is async apart from changing modes and MSP2WIFI
    // No need to run balls-to-the-wall; the wifi runs on this core too (0)
    return 2;
  }

  // if webupdate was requested before or .wifi_auto_on_interval has elapsed but uart is not detected
  // start webupdate, there might be wrong configuration flashed.
  if(firmwareOptions.wifi_auto_on_interval != -1 && webserverPreventAutoStart == false && connectionState < wifiUpdate && !wifiStarted){
    setWifiUpdateMode();
    return DURATION_IMMEDIATELY;
  }
  return DURATION_NEVER;
}

device_t WIFI_device = {
  .initialize = initialize,
  .start = start,
  .event = event,
  .timeout = timeout,
  .subscribe = EVENT_CONNECTION_CHANGED
};
