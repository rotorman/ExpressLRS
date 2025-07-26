#include "rxtx_devLua.h"
#include "CRSF.h"
#include "CRSFHandset.h"
#include "OTA.h"
#include "FHSS.h"
#include "helpers.h"

#define STR_LUA_ALLAUX         "AUX1;AUX2;AUX3;AUX4;AUX5;AUX6;AUX7;AUX8;AUX9;AUX10"

#define STR_LUA_ALLAUX_UPDOWN  "AUX1" LUASYM_ARROW_UP ";AUX1" LUASYM_ARROW_DN ";AUX2" LUASYM_ARROW_UP ";AUX2" LUASYM_ARROW_DN \
                               ";AUX3" LUASYM_ARROW_UP ";AUX3" LUASYM_ARROW_DN ";AUX4" LUASYM_ARROW_UP ";AUX4" LUASYM_ARROW_DN \
                               ";AUX5" LUASYM_ARROW_UP ";AUX5" LUASYM_ARROW_DN ";AUX6" LUASYM_ARROW_UP ";AUX6" LUASYM_ARROW_DN \
                               ";AUX7" LUASYM_ARROW_UP ";AUX7" LUASYM_ARROW_DN ";AUX8" LUASYM_ARROW_UP ";AUX8" LUASYM_ARROW_DN \
                               ";AUX9" LUASYM_ARROW_UP ";AUX9" LUASYM_ARROW_DN ";AUX10" LUASYM_ARROW_UP ";AUX10" LUASYM_ARROW_DN

#if defined(RADIO_SX128X)
#define STR_LUA_PACKETRATES \
    "50Hz(-115dBm);100Hz Full(-112dBm);150Hz(-112dBm);250Hz(-108dBm);333Hz Full(-105dBm);500Hz(-105dBm);" \
    "D250(-104dBm);D500(-104dBm);F500(-104dBm);F1000(-104dBm)"
#else
#error Invalid radio configuration!
#endif

#define HAS_RADIO (GPIO_PIN_SCK != UNDEF_PIN)

static char version_domain[20+1+6+1];
char pwrFolderDynamicName[] = "TX Power (1000 Dynamic)";
static char modelMatchUnit[] = " (ID: 00)";
static char tlmBandwidth[] = " (xxxxxbps)";
static const char folderNameSeparator[2] = {' ',':'};
static const char tlmRatios[] = "Std;Off;1:128;1:64;1:32;1:16;1:8;1:4;1:2;Race";
static const char switchmodeOpts4ch[] = "Wide;Hybrid";
static const char switchmodeOpts4chMav[] = ";Hybrid";
static const char switchmodeOpts8ch[] = "8ch;16ch Rate/2;12ch Mixed";
static const char switchmodeOpts8chMav[] = ";16ch Rate/2;";
static const char antennamodeOpts[] = "Gemini;Ant 1;Ant 2;Switch";
static const char antennamodeOptsDualBand[] = "Gemini;;;";
static const char luastrOffOn[] = "Off;On";
static char luastrPacketRates[] = STR_LUA_PACKETRATES;

static struct luaItem_selection luaAirRate = {
    {"Packet Rate", CRSF_TEXT_SELECTION},
    0, // value
    luastrPacketRates,
    STR_EMPTYSPACE
};

static struct luaItem_selection luaTlmRate = {
    {"Telem Ratio", CRSF_TEXT_SELECTION},
    0, // value
    tlmRatios,
    tlmBandwidth
};

//----------------------------POWER------------------
static struct luaItem_folder luaPowerFolder = {
    {"TX Power", CRSF_FOLDER},pwrFolderDynamicName
};

static struct luaItem_selection luaPower = {
    {"Max Power", CRSF_TEXT_SELECTION},
    0, // value
    strPowerLevels,
    "mW"
};

static struct luaItem_selection luaDynamicPower = {
    {"Dynamic", CRSF_TEXT_SELECTION},
    0, // value
    "Off;Dyn;AUX9;AUX10;AUX11;AUX12",
    STR_EMPTYSPACE
};

static struct luaItem_selection luaFanThreshold = {
    {"Fan Thresh", CRSF_TEXT_SELECTION},
    0, // value
    "10mW;25mW;50mW;100mW;250mW;500mW;1000mW;2000mW;Never",
    STR_EMPTYSPACE // units embedded so it won't display "NevermW"
};

//----------------------------POWER------------------

static struct luaItem_selection luaSwitch = {
    {"Switch Mode", CRSF_TEXT_SELECTION},
    0, // value
    switchmodeOpts4ch,
    STR_EMPTYSPACE
};

static struct luaItem_selection luaAntenna = {
    {"Antenna Mode", CRSF_TEXT_SELECTION},
    0, // value
    antennamodeOpts,
    STR_EMPTYSPACE
};

static struct luaItem_selection luaModelMatch = {
    {"Model Match", CRSF_TEXT_SELECTION},
    0, // value
    luastrOffOn,
    modelMatchUnit
};

static struct luaItem_command luaBind = {
    {"Bind", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static struct luaItem_string luaInfo = {
    {"Bad/Good", (crsf_value_type_e)(CRSF_INFO | CRSF_FIELD_ELRS_HIDDEN)},
    STR_EMPTYSPACE
};

static struct luaItem_string luaELRSversion = {
    {version_domain, CRSF_INFO},
    commit
};

//---------------------------- WiFi -----------------------------
static struct luaItem_folder luaWiFiFolder = {
    {"WiFi Connectivity", CRSF_FOLDER}
};

static struct luaItem_command luaWebUpdate = {
    {"Enable WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static struct luaItem_command luaRxWebUpdate = {
    {"Enable Rx WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};
//---------------------------- WiFi -----------------------------

static char luaBadGoodString[10];

extern TxConfig config;
extern void ResetPower();
extern uint8_t adjustPacketRateForBaud(uint8_t rate);
extern void SetSyncSpam();
extern bool RxWiFiReadyToSend;
extern unsigned long rebootTime;
extern void setWifiUpdateMode();

static void luadevUpdateModelID() {
  itoa(CRSFHandset::getModelID(), modelMatchUnit+6, 10);
  strcat(modelMatchUnit, ")");
}

static void luadevUpdateTlmBandwidth()
{
  expresslrs_tlm_ratio_e eRatio = (expresslrs_tlm_ratio_e)config.GetTlm();
  // TLM_RATIO_STD / TLM_RATIO_DISARMED
  if (eRatio == TLM_RATIO_STD || eRatio == TLM_RATIO_DISARMED)
  {
    // For Standard ratio, display the ratio instead of bps
    strcpy(tlmBandwidth, " (1:");
    uint8_t ratioDiv = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
    itoa(ratioDiv, &tlmBandwidth[4], 10);
    strcat(tlmBandwidth, ")");
  }

  // TLM_RATIO_NO_TLM
  else if (eRatio == TLM_RATIO_NO_TLM)
  {
    tlmBandwidth[0] = '\0';
  }

  // All normal ratios
  else
  {
    tlmBandwidth[0] = ' ';

    uint16_t hz = 1000000 / ExpressLRS_currAirRate_Modparams->interval;
    uint8_t ratiodiv = TLMratioEnumToValue(eRatio);
    uint8_t burst = TLMBurstMaxForRateRatio(hz, ratiodiv);
    uint8_t bytesPerCall = OtaIsFullRes ? ELRS8_TELEMETRY_BYTES_PER_CALL : ELRS4_TELEMETRY_BYTES_PER_CALL;
    uint32_t bandwidthValue = bytesPerCall * 8U * burst * hz / ratiodiv / (burst + 1);
    if (OtaIsFullRes)
    {
      // Due to fullres also packing telemetry into the LinkStats packet, there is at least
      // N bytes more data for every rate except 100Hz 1:128, and 2*N bytes more for many
      // rates. The calculation is a more complex though, so just approximate some of the
      // extra bandwidth
      bandwidthValue += 8U * (ELRS8_TELEMETRY_BYTES_PER_CALL - sizeof(OTA_LinkStats_s));
    }

    itoa(bandwidthValue, &tlmBandwidth[2], 10);
    strcat(tlmBandwidth, "bps)");
  }
}

static void luahandWifiBle(struct luaPropertiesCommon *item, uint8_t arg)
{
  struct luaItem_command *cmd = (struct luaItem_command *)item;
  void (*setTargetState)();
  connectionState_e targetState;
  const char *textConfirm;
  const char *textRunning;
  if ((void *)item == (void *)&luaWebUpdate)
  {
    setTargetState = &setWifiUpdateMode;
    textConfirm = "Enter WiFi Update?";
    textRunning = "WiFi Running...";
    targetState = wifiUpdate;
  }

  switch ((luaCmdStep_e)arg)
  {
    case lcsClick:
      if (connectionState == connected)
      {
        sendLuaCommandResponse(cmd, lcsAskConfirm, textConfirm);
        return;
      }
      // fallthrough (clicking while not connected goes right to exectute)

    case lcsConfirmed:
      sendLuaCommandResponse(cmd, lcsExecuting, textRunning);
      setTargetState();
      break;

    case lcsCancel:
      sendLuaCommandResponse(cmd, lcsIdle, STR_EMPTYSPACE);
      if (connectionState == targetState)
      {
        rebootTime = millis() + 400;
      }
      break;

    default: // LUACMDSTEP_NONE on load, LUACMDSTEP_EXECUTING (our lua) or LUACMDSTEP_QUERY (Crossfire Config)
      sendLuaCommandResponse(cmd, cmd->step, cmd->info);
      break;
  }
}

static void luahandSimpleSendCmd(struct luaPropertiesCommon *item, uint8_t arg)
{
  const char *msg = "Sending...";
  static uint32_t lastLcsPoll;
  if (arg < lcsCancel)
  {
    lastLcsPoll = millis();
    if ((void *)item == (void *)&luaBind)
    {
      msg = "Binding...";
      EnterBindingModeSafely();
    }
    else if ((void *)item == (void *)&luaRxWebUpdate)
    {
      RxWiFiReadyToSend = true;
    }
    sendLuaCommandResponse((struct luaItem_command *)item, lcsExecuting, msg);
  } /* if doExecute */
  else if(arg == lcsCancel || ((millis() - lastLcsPoll)> 2000))
  {
    sendLuaCommandResponse((struct luaItem_command *)item, lcsIdle, STR_EMPTYSPACE);
  }
}

static void updateFolderName_TxPower()
{
  uint8_t txPwrDyn = config.GetDynamicPower() ? config.GetBoostChannel() + 1 : 0;
  uint8_t pwrFolderLabelOffset = 10; // start writing after "TX Power ("

  // Power Level
  pwrFolderLabelOffset += findLuaSelectionLabel(&luaPower, &pwrFolderDynamicName[pwrFolderLabelOffset], config.GetPower() - MinPower);

  // Dynamic Power
  if (txPwrDyn)
  {
    pwrFolderDynamicName[pwrFolderLabelOffset++] = folderNameSeparator[0];
    pwrFolderLabelOffset += findLuaSelectionLabel(&luaDynamicPower, &pwrFolderDynamicName[pwrFolderLabelOffset], txPwrDyn);
  }

  pwrFolderDynamicName[pwrFolderLabelOffset++] = ')';
  pwrFolderDynamicName[pwrFolderLabelOffset] = '\0';
}

/***
 * @brief: Update the luaBadGoodString with the current bad/good count
 * This item is hidden on our Lua and only displayed in other systems that don't poll our status
 * Called from luaRegisterDevicePingCallback
 ****/
static void luadevUpdateBadGood()
{
  itoa(CRSFHandset::BadPktsCountResult, luaBadGoodString, 10);
  strcat(luaBadGoodString, "/");
  itoa(CRSFHandset::GoodPktsCountResult, luaBadGoodString + strlen(luaBadGoodString), 10);
}

/***
 * @brief: Update the dynamic strings used for folder names and labels
 ***/
void luadevUpdateFolderNames()
{
  updateFolderName_TxPower();

  // These aren't folder names, just string labels slapped in the units field generally
  luadevUpdateTlmBandwidth();
}

static void recalculatePacketRateOptions(int minInterval)
{
    const char *allRates = STR_LUA_PACKETRATES;
    const char *pos = allRates;
    luastrPacketRates[0] = 0;
    for (int i=0 ; i < RATE_MAX ; i++)
    {
        uint8_t rate = i;
        rate = RATE_MAX - 1 - rate;
        bool rateAllowed = (get_elrs_airRateConfig(rate)->interval * get_elrs_airRateConfig(rate)->numOfSends) >= minInterval;
        const char *semi = strchrnul(pos, ';');
        if (rateAllowed)
        {
            strncat(luastrPacketRates, pos, semi - pos);
        }
        pos = semi;
        if (*semi == ';')
        {
            strcat(luastrPacketRates, ";");
            pos = semi+1;
        }
    }

    // trim off trailing semicolons (assumes luastrPacketRates has at least 1 non-semicolon)
    for (auto lastPos = strlen(luastrPacketRates)-1; luastrPacketRates[lastPos] == ';'; lastPos--)
    {
        luastrPacketRates[lastPos] = '\0';
    }
}

uint8_t adjustSwitchModeForAirRate(OtaSwitchMode_e eSwitchMode, uint8_t packetSize)
{
  // Only the fullres modes have 3 switch modes, so reset the switch mode if outside the
  // range for 4ch mode
  if (packetSize == OTA4_PACKET_SIZE)
  {
    if (eSwitchMode > smHybridOr16ch)
      return smWideOr8ch;
  }

  return eSwitchMode;
}

static void registerLuaParameters()
{
  if (HAS_RADIO) {
    registerLUAParameter(&luaAirRate, [](struct luaPropertiesCommon *item, uint8_t arg) {
      if (arg < RATE_MAX)
      {
        uint8_t selectedRate = RATE_MAX - 1 - arg;
        uint8_t actualRate = adjustPacketRateForBaud(selectedRate);
        uint8_t newSwitchMode = adjustSwitchModeForAirRate(
          (OtaSwitchMode_e)config.GetSwitchMode(), get_elrs_airRateConfig(actualRate)->PayloadLength);
        // If the switch mode is going to change, block the change while connected
        bool isDisconnected = connectionState == disconnected;
        if (newSwitchMode == OtaSwitchModeCurrent || isDisconnected)
        {
          config.SetRate(actualRate);
          config.SetSwitchMode(newSwitchMode);
          if (actualRate != selectedRate)
          {
            setLuaWarningFlag(LUA_FLAG_ERROR_BAUDRATE, true);
          }
        }
        else
        {
          setLuaWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
        }
      }
    });
    registerLUAParameter(&luaTlmRate, [](struct luaPropertiesCommon *item, uint8_t arg) {
      expresslrs_tlm_ratio_e eRatio = (expresslrs_tlm_ratio_e)arg;
      if (eRatio <= TLM_RATIO_DISARMED)
      {
        config.SetTlm(eRatio);
      }
    });
    registerLUAParameter(&luaSwitch, [](struct luaPropertiesCommon *item, uint8_t arg) {
      // Only allow changing switch mode when disconnected since we need to guarantee
      // the pack and unpack functions are matched
      bool isDisconnected = connectionState == disconnected;
      if (isDisconnected)
      {
        config.SetSwitchMode(arg);
        OtaUpdateSerializers((OtaSwitchMode_e)arg, ExpressLRS_currAirRate_Modparams->PayloadLength);
      }
      else
      {
        setLuaWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
      }
    });
    registerLUAParameter(&luaModelMatch, [](struct luaPropertiesCommon *item, uint8_t arg) {
      bool newModelMatch = arg;
      config.SetModelMatch(newModelMatch);
      if (connectionState == connected)
      {
        mspPacket_t msp;
        msp.reset();
        msp.makeCommand();
        msp.function = MSP_SET_RX_CONFIG;
        msp.addByte(MSP_ELRS_MODEL_ID);
        msp.addByte(newModelMatch ? CRSFHandset::getModelID() : 0xff);
        CRSF::AddMspMessage(&msp, CRSF_ADDRESS_CRSF_RECEIVER);
      }
      luadevUpdateModelID();
    });

    // POWER folder
    registerLUAParameter(&luaPowerFolder);
    luadevGeneratePowerOpts(&luaPower);
    registerLUAParameter(&luaPower, [](struct luaPropertiesCommon *item, uint8_t arg) {
      config.SetPower((PowerLevels_e)constrain(arg + POWERMGNT::getMinPower(), POWERMGNT::getMinPower(), POWERMGNT::getMaxPower()));
      if (!config.IsModified())
      {
          ResetPower();
      }
    }, luaPowerFolder.common.id);
    registerLUAParameter(&luaDynamicPower, [](struct luaPropertiesCommon *item, uint8_t arg) {
      config.SetDynamicPower(arg > 0);
      config.SetBoostChannel((arg - 1) > 0 ? arg - 1 : 0);
    }, luaPowerFolder.common.id);
  }
  if (GPIO_PIN_FAN_EN != UNDEF_PIN || GPIO_PIN_FAN_PWM != UNDEF_PIN) {
    registerLUAParameter(&luaFanThreshold, [](struct luaPropertiesCommon *item, uint8_t arg){
      config.SetPowerFanThreshold(arg);
    }, luaPowerFolder.common.id);
  }

  // WIFI folder
  registerLUAParameter(&luaWiFiFolder);
  registerLUAParameter(&luaWebUpdate, &luahandWifiBle, luaWiFiFolder.common.id);
  if (HAS_RADIO) {
    registerLUAParameter(&luaRxWebUpdate, &luahandSimpleSendCmd, luaWiFiFolder.common.id);
  }

  if (HAS_RADIO) {
    registerLUAParameter(&luaBind, &luahandSimpleSendCmd);
  }

  registerLUAParameter(&luaInfo);
  if (strlen(version) < 21) {
    strlcpy(version_domain, version, 21);
    strlcat(version_domain, " ", sizeof(version_domain));
  } else {
    strlcpy(version_domain, version, 18);
    strlcat(version_domain, "... ", sizeof(version_domain));
  }
  strlcat(version_domain, FHSSconfig->domain, sizeof(version_domain));
  registerLUAParameter(&luaELRSversion);
}

static int event()
{
  if (connectionState > FAILURE_STATES)
  {
    return DURATION_NEVER;
  }

  uint8_t currentRate = adjustPacketRateForBaud(config.GetRate());
  recalculatePacketRateOptions(handset->getMinPacketInterval());
  setLuaTextSelectionValue(&luaAirRate, RATE_MAX - 1 - currentRate);

  setLuaTextSelectionValue(&luaTlmRate, config.GetTlm());
  luaTlmRate.options = tlmRatios;

  luaAntenna.options = antennamodeOpts;

  setLuaTextSelectionValue(&luaSwitch, config.GetSwitchMode());
  luaSwitch.options = OtaIsFullRes ? switchmodeOpts8ch : switchmodeOpts4ch;

  luadevUpdateModelID();
  setLuaTextSelectionValue(&luaModelMatch, (uint8_t)config.GetModelMatch());
  setLuaTextSelectionValue(&luaPower, config.GetPower() - MinPower);
  if (GPIO_PIN_FAN_EN != UNDEF_PIN || GPIO_PIN_FAN_PWM != UNDEF_PIN)
  {
    setLuaTextSelectionValue(&luaFanThreshold, config.GetPowerFanThreshold());
  }

  uint8_t dynamic = config.GetDynamicPower() ? config.GetBoostChannel() + 1 : 0;
  setLuaTextSelectionValue(&luaDynamicPower, dynamic);

  luadevUpdateFolderNames();
  return DURATION_IMMEDIATELY;
}

static int timeout()
{
  if (luaHandleUpdateParameter())
  {
    SetSyncSpam();
  }
  return DURATION_IMMEDIATELY;
}

static int start()
{
  if (connectionState > FAILURE_STATES)
  {
    return DURATION_NEVER;
  }
  handset->registerParameterUpdateCallback(luaParamUpdateReq);
  registerLuaParameters();

  setLuaStringValue(&luaInfo, luaBadGoodString);
  luaRegisterDevicePingCallback(&luadevUpdateBadGood);

  event();
  return DURATION_IMMEDIATELY;
}

device_t LUA_device = {
  .initialize = nullptr,
  .start = start,
  .event = event,
  .timeout = timeout,
  .subscribe = EVENT_ALL
};
