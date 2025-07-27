#include "targets.h"
#include "common.h"
#include "config.h"
#include "CRSF.h"
#include "helpers.h"
#include "hwTimer.h"
#include "CRSFHandset.h"
#include "lua.h"
#include "msp.h"
#include "msptypes.h"
#include "telemetry_protocol.h"
#include "devHandset.h"
#include "devADC.h"
#include "devLED.h"
#include "devWIFI.h"
#include "devButton.h"
#include "devScreen.h"

#if defined(PLATFORM_ESP32_S3)
#include "USB.h"
#define USBSerial Serial
#endif

//// CONSTANTS ////
#define MSP_PACKET_SEND_INTERVAL 10LU
#define RF_RC_INTERVAL_US 20000U

/// define some libs to use ///
MSP msp;
ELRS_EEPROM eeprom;
TxConfig config;

extern device_t LUA_device;
unsigned long rebootTime = 0;
extern bool webserverPreventAutoStart;

volatile uint32_t LastTLMpacketRecvMillis = 0;
uint32_t TLMpacketReported = 0;
static bool commitInProgress = false;

volatile bool busyTransmitting;
static volatile bool ModelUpdatePending;

#define BindingSpamAmount 25
static uint8_t BindingSendCount;

device_affinity_t ui_devices[] = {
  {&Handset_device, 1},
  {&LED_device, 0},
  {&RGB_device, 0},
  {&LUA_device, 1},
  {&ADC_device, 1},
  {&WIFI_device, 0},
  {&Button_device, 0},
  {&Screen_device, 0},
};

extern void setupTargetCommon();

void ICACHE_RAM_ATTR SendRCdataToRF()
{
  // Do not send a stale channels packet to the RX if one has not been received from the handset
  // *Do* send data if a packet has never been received from handset and the timer is running
  // this is the case when bench testing and TXing without a handset
  //bool dontSendChannelData = false;
  uint32_t lastRcData = handset->GetRCdataLastRecv();
  if (lastRcData && (micros() - lastRcData > 1000000))
  {
    // Do not send stale or fake zero packet RC!
    return;
  }

  busyTransmitting = true;
  // TODO! Remove fake:
  busyTransmitting = false;
  connectionHasModelMatch = true;
  uint32_t const now = millis();
  LastTLMpacketRecvMillis = now;
}

/*
 * Called as the TOCK timer ISR when there is a CRSF connection from the handset
 */
void ICACHE_RAM_ATTR timerCallback()
{
  /* If we are busy writing to EEPROM (committing config changes) then we just advance the nonces, i.e. no SPI traffic */
  if (commitInProgress)
  {
    return;
  }

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
  webserverPreventAutoStart = true;

  if (connectionState == noCrossfire || connectionState < MODE_STATES)
  {
    // When CRSF first connects, always go into a brief delay before
    // starting to transmit, to make sure a ModelID update isn't coming
    // right behind it
    setConnectionState(awaitingModelId);
  }
  // But start the timer to get OpenTX sync going and a ModelID update sent
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
    (uint32_t)ExpressLRS_currTlmDenom * RF_RC_INTERVAL_US / (1000U / RX_LOSS_CNT)
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

static void EnterBindingMode()
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

void EnterBindingModeSafely()
{
  // TX can always enter binding mode safely as the function handles stopping the transmitter
  EnterBindingMode();
}

void ParseMSPData(uint8_t *buf, uint8_t size)
{
  for (uint8_t i = 0; i < size; ++i)
  {
    if (msp.processReceivedByte(buf[i]))
    {
      msp.getReceivedPacket();
      msp.markPacketReceived();
    }
  }
}

/**
 * Target-specific initialization code called early in setup()
 * Setup GPIOs or other hardware, config not yet loaded
 ***/
static void setupTarget()
{
  setupTargetCommon();
}

bool setupHardwareFromOptions()
{
  if (!options_init())
  {
    // Register the WiFi with the framework
    static device_affinity_t wifi_device[] = {
        {&WIFI_device, 1}
    };
    devicesRegister(wifi_device, ARRAY_SIZE(wifi_device));
    devicesInit();

    setConnectionState(hardwareUndefined);
    return false;
  }
  return true;
}

void setup()
{
  if (setupHardwareFromOptions())
  {
    setupTarget();
    // Register the devices with the framework
    devicesRegister(ui_devices, ARRAY_SIZE(ui_devices));
    // Initialise the devices
    devicesInit();

    handset->registerCallbacks(UARTconnected, UARTdisconnected, ModelUpdateReq, EnterBindingModeSafely);

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
      handset->setPacketInterval(RF_RC_INTERVAL_US);
      //setConnectionState(disconnected);
      hwTimer::init(nullptr, timerCallback);
      setConnectionState(noCrossfire);
    }
  }
  
  registerButtonFunction(ACTION_BIND, EnterBindingMode);
  
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

  //executeDeferredFunction(micros());

  if (connectionState > MODE_STATES)
  {
    return;
  }

  /* Send TLM updates to handset if connected + reporting period
   * is elapsed. This keeps handset happy dispite of the telemetry ratio */
  if ((connectionState == connected) && (LastTLMpacketRecvMillis != 0) &&
      (now >= (uint32_t)(firmwareOptions.tlm_report_interval + TLMpacketReported)))
  {
    uint8_t linkStatisticsFrame[CRSF_FRAME_NOT_COUNTED_BYTES + CRSF_FRAME_SIZE(sizeof(crsfLinkStatistics_t))];

    CRSFHandset::makeLinkStatisticsPacket(linkStatisticsFrame);
    handset->sendTelemetryToTX(linkStatisticsFrame);
    TLMpacketReported = now;
  }

  /*
  if (TelemetryReceiver.HasFinishedData())
  {
      if (CRSFinBuffer[0] != CRSF_ADDRESS_USB)
      {
        // Send all other tlm to handset
        handset->sendTelemetryToTX(CRSFinBuffer);
      }
      TelemetryReceiver.Unlock();
  }
  */

  // only send msp data when binding is not active
  if (InBindingMode)
  {
    // exit bind mode if package after some repeats
    if (BindingSendCount > BindingSpamAmount) {
      ExitBindingMode();
    }
  }
  /*
  else if (!MspSender.IsActive())
  {
    // sending is done and we need to update our flag
    if (mspTransferActive)
    {
      // unlock buffer for msp messages
      CRSF::UnlockMspMessage();
      mspTransferActive = false;
    }
    // we are not sending so look for next msp package
    else
    {
      uint8_t* mspData;
      uint8_t mspLen;
      CRSF::GetMspMessage(&mspData, &mspLen);
      // if we have a new msp package start sending
      if (mspData != nullptr)
      {
        MspSender.SetDataToTransmit(mspData, mspLen);
        mspTransferActive = true;
      }
    }
  }
  */
  
}
