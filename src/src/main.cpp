#include "targets.h"
#include "common.h"
#include "config.h"
#include "CRSF.h"
#include "helpers.h"
#include "hwTimer.h"
#include "CRSFHandset.h"
#include "lua.h"
#include "devHandset.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

#if defined(PLATFORM_ESP32_S3)
#include "USB.h"
#define USBSerial Serial
#endif

//// CONSTANTS ////

/***** TODO! Adjust the values in this section to YOUR setup! *****/

// Model receiver's MAC address(es) - replace with YOUR CyberBrick receiver Core MAC address(es)!
// The example below lists 3 models. If you wish to control only one model, remove the bottom two lines (models 1 and 2).
// You can control/add up to 20 models to the list below (limitation of ESP32 ESP-NOW peer address table).
uint8_t cyberbrickRxMAC[][6] =
  {
    {0xa1, 0xb1, 0xc1, 0xd1, 0xe1, 0xf1}, // Model 0 receiver MAC address
    {0xa2, 0xb2, 0xc2, 0xd2, 0xe2, 0xf2}, // Model 1 receiver MAC address
    {0xa3, 0xb3, 0xc3, 0xd3, 0xe3, 0xf3}  // Model 2 receiver MAC address
  };
// You can pick a model to control in EdgeTX under:
// MODEL -> Internal RF or External RF -> Receiver <number>
// where the number matches the model number in the above list.

/******************************************************************/

#define RF_RC_INTERVAL_US 20000U

/// define some libs to use ///
ELRS_EEPROM eeprom;
TxConfig config;

// Connection state information
uint8_t UID[UID_LEN] = {0};  // "bind phrase" ID
bool connectionHasModelMatch = false;
bool InBindingMode = false;
connectionState_e connectionState = disconnected;

// Current state of channels, CRSF format
volatile uint16_t ChannelData[CRSF_NUM_CHANNELS];

extern device_t LUA_device;
unsigned long rebootTime = 0;

volatile uint32_t LastTLMpacketRecvMillis = 0;
uint32_t TLMpacketReported = 0;

volatile bool busyTransmitting;
static volatile bool ModelUpdatePending;

#define BindingSpamAmount 25
static uint8_t BindingSendCount;

esp_now_peer_info_t peerInfo;

device_affinity_t ui_devices[] = {
  {&Handset_device, 1},
  {&LUA_device, 1},
};

void ICACHE_RAM_ATTR SendRCdataToRF()
{
  // Do not send a stale channels packet to the RX if one has not been received from the handset
  // *Do* send data if a packet has never been received from handset and the timer is running
  // this is the case when bench testing
  uint32_t lastRcDataUS = handset->GetRCdataLastRecvUS();
  if (lastRcDataUS && (micros() - lastRcDataUS > 1000000))
  {
    // Do not send stale or fake zero packet RC!
    return;
  }

    // Send message via ESP-NOW
  uint8_t modelid = CRSFHandset::getModelID();
  if (modelid <= sizeof(cyberbrickRxMAC)/6) // Plausibility check that we are not accessing cyberbrickRxMAC array out of bounds
  {
    busyTransmitting = true;
    esp_now_send(cyberbrickRxMAC[modelid], (uint8_t *) &ChannelData, sizeof(ChannelData));
    connectionHasModelMatch = true;
  }
}

/*
 * Called as the TOCK timer ISR when there is a CRSF connection from the handset
 */
void ICACHE_RAM_ATTR timerCallback()
{
  // Sync EdgeTX to this point
  handset->JustSentRFpacket();

  // Do not transmit until in disconnected/connected state
  if (connectionState == awaitingModelId)
    return;

  SendRCdataToRF();
}

static void UARTdisconnected()
{
  hwTimer::stop();
  setConnectionState(noCrossfire);
}

static void UARTconnected()
{
  if (connectionState == noCrossfire || connectionState < MODE_STATES)
  {
    // When CRSF first connects, always go into a brief delay before
    // starting to transmit, to make sure a ModelID update isn't coming
    // right behind it
    setConnectionState(awaitingModelId);
  }
  // But start the timer to get EdgeTX sync going and a ModelID update sent
  hwTimer::resume();
}

void ModelUpdateReq()
{
  // Force synspam with the current rate parameters in case already have a connection established
  if (config.SetModelId(CRSFHandset::getModelID()))
  {
    ModelUpdatePending = true;
  }

  devicesTriggerEvent(EVENT_MODEL_SELECTED);

  // Jump from awaitingModelId to transmitting to break the startup delay now
  // that the ModelID has been confirmed by the handset
  if (connectionState == awaitingModelId)
  {
    setConnectionState(disconnected);
  }
}

static void UpdateConnectDisconnectStatus()
{
  // Number of telemetry packets which can be lost in a row before going to disconnected state
  constexpr unsigned RX_LOSS_CNT = 5;
  // Must be at least 512ms and +2 to account for any rounding down and partial millis()
  const uint32_t msConnectionLostTimeout = std::max((uint32_t)512U,
    (uint32_t)RF_RC_INTERVAL_US / (1000U / RX_LOSS_CNT)
    ) + 2U;
  // Capture the last before now so it will always be <= now
  const uint32_t lastTlmMillis = LastTLMpacketRecvMillis;
  const uint32_t now = millis();
  if (lastTlmMillis && ((now - lastTlmMillis) <= msConnectionLostTimeout))
  {
    if (connectionState != connected)
    {
      setConnectionState(connected);
      CRSFHandset::ForwardDevicePings = true;
    }
  }
  // TODO! Check for disconnection
}

void EnterBindingMode()
{
  if (InBindingMode)
      return;

  // Disable the TX timer and wait for any TX to complete
  hwTimer::stop();
  while (busyTransmitting);

  // Binding uses 50Hz, and InvertIQ
  InBindingMode = true;

  // Start transmitting again
  hwTimer::resume();
}

static void ExitBindingMode()
{
  if (!InBindingMode)
    return;

  InBindingMode = false;
  UARTconnected();
}

// ESP-NOW callback, called when data is sent
void ESPNOW_OnDataSentCB(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS)
  {
    uint32_t const now = millis();
    LastTLMpacketRecvMillis = now;
  }
  busyTransmitting = false;
}

bool initESPNOW()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  WiFi.begin("network-name", "pass-to-network", 1);
  WiFi.disconnect();

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    ESP.restart();
  }

  // Register peers
  memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
  peerInfo.channel = 0;
  peerInfo.encrypt = false;  
  bool bResult = true;
  for (int i = 0; i < sizeof(cyberbrickRxMAC)/6; i++)
  { 
    // Iterate through the peer addresses
    memcpy(peerInfo.peer_addr, cyberbrickRxMAC[i], 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      bResult = false;
    }
  }
  
  // Register callback to get the status of the transmitted ESP-NOW packet
  if (esp_now_register_send_cb(ESPNOW_OnDataSentCB) != ESP_OK)
    bResult = false;
  
  return bResult;
}

bool setupHardwareFromOptions()
{
  if (!options_init())
  {
    setConnectionState(hardwareUndefined);
    return false;
  }

  // TODO! Add timeout or bailout
  while (!initESPNOW()) {}

  return true;
}

void setup()
{
  if (setupHardwareFromOptions())
  {
    // Register the devices with the framework
    devicesRegister(ui_devices, ARRAY_SIZE(ui_devices));
    // Initialise the devices
    devicesInit();

    handset->registerCallbacks(UARTconnected, UARTdisconnected, ModelUpdateReq, EnterBindingMode);

    eeprom.Begin(); // Init the eeprom
    config.SetStorageProvider(&eeprom); // Pass pointer to the Config class for access to storage
    config.Load(); // Load the stored values from eeprom

    bool init_success = true;

    if (!init_success)
    {
      setConnectionState(radioFailed);
    }
    else
    {
      hwTimer::updateInterval(RF_RC_INTERVAL_US);
      handset->setPacketIntervalUS(RF_RC_INTERVAL_US);
      //setConnectionState(disconnected);
      hwTimer::init(nullptr, timerCallback);
      setConnectionState(noCrossfire);
    }
  }

  devicesStart();
}

void loop()
{
  uint32_t now = millis();

  if (connectionState < MODE_STATES)
  {
    UpdateConnectDisconnectStatus();
  }

  // Update UI devices
  devicesUpdate(now);

  // If the reboot time is set and the current time is past the reboot time then reboot.
  if (rebootTime != 0 && now > rebootTime) {
    ESP.restart();
  }

  if (connectionState > MODE_STATES)
  {
    return;
  }

  /* Send TLM updates to handset if connected + reporting period
   * is elapsed. This keeps handset happy dispite of the telemetry ratio */
  if ((connectionState == connected) && (LastTLMpacketRecvMillis != 0) &&
      (now >= (uint32_t)(240U + TLMpacketReported)))
  {
    uint8_t linkStatisticsFrame[CRSF_FRAME_NOT_COUNTED_BYTES + CRSF_FRAME_SIZE(sizeof(crsfLinkStatistics_t))];

    CRSFHandset::makeLinkStatisticsPacket(linkStatisticsFrame);
    handset->sendTelemetryToTX(linkStatisticsFrame);
    TLMpacketReported = now;
  }

  // only send msp data when binding is not active
  if (InBindingMode)
  {
    // exit bind mode if package after some repeats
    if (BindingSendCount > BindingSpamAmount) {
      ExitBindingMode();
    }
  }
}
