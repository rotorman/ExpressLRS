#include "common.h"
#include "device.h"
#include "lua.h"
#include "config.h"
#include "CRSF.h"
#include "CRSFHandset.h"
#include "helpers.h"

const char STR_EMPTYSPACE[] = { 0 };

#define STR_LUA_ALLAUX         "AUX1;AUX2;AUX3;AUX4;AUX5;AUX6;AUX7;AUX8;AUX9;AUX10"

#define STR_LUA_ALLAUX_UPDOWN  "AUX1" LUASYM_ARROW_UP ";AUX1" LUASYM_ARROW_DN ";AUX2" LUASYM_ARROW_UP ";AUX2" LUASYM_ARROW_DN \
                               ";AUX3" LUASYM_ARROW_UP ";AUX3" LUASYM_ARROW_DN ";AUX4" LUASYM_ARROW_UP ";AUX4" LUASYM_ARROW_DN \
                               ";AUX5" LUASYM_ARROW_UP ";AUX5" LUASYM_ARROW_DN ";AUX6" LUASYM_ARROW_UP ";AUX6" LUASYM_ARROW_DN \
                               ";AUX7" LUASYM_ARROW_UP ";AUX7" LUASYM_ARROW_DN ";AUX8" LUASYM_ARROW_UP ";AUX8" LUASYM_ARROW_DN \
                               ";AUX9" LUASYM_ARROW_UP ";AUX9" LUASYM_ARROW_DN ";AUX10" LUASYM_ARROW_UP ";AUX10" LUASYM_ARROW_DN


#define HAS_RADIO (GPIO_PIN_SCK != UNDEF_PIN)

static char version_domain[20+1+6+1];
static char modelMatchUnit[] = " (ID: 00)";
static const char folderNameSeparator[2] = {' ',':'};
static const char luastrOffOn[] = "Off;On";

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

static char luaBadGoodString[10];

extern TxConfig config;
extern unsigned long rebootTime;

static void luadevUpdateModelID() {
  itoa(CRSFHandset::getModelID(), modelMatchUnit+6, 10);
  strcat(modelMatchUnit, ")");
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
      EnterBindingMode();
    }
    sendLuaCommandResponse((struct luaItem_command *)item, lcsExecuting, msg);
  } /* if doExecute */
  else if(arg == lcsCancel || ((millis() - lastLcsPoll)> 2000))
  {
    sendLuaCommandResponse((struct luaItem_command *)item, lcsIdle, STR_EMPTYSPACE);
  }
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

static void registerLuaParameters()
{
  registerLUAParameter(&luaModelMatch, [](struct luaPropertiesCommon *item, uint8_t arg) {
    bool newModelMatch = arg;
    config.SetModelMatch(newModelMatch);
    luadevUpdateModelID();
  });

  registerLUAParameter(&luaBind, &luahandSimpleSendCmd);
  
  registerLUAParameter(&luaInfo);
  if (strlen(version) < 21) {
    strlcpy(version_domain, version, 21);
    strlcat(version_domain, " ", sizeof(version_domain));
  } else {
    strlcpy(version_domain, version, 18);
    strlcat(version_domain, "... ", sizeof(version_domain));
  }
  strlcat(version_domain, "ESP-NOW", sizeof(version_domain));
  registerLUAParameter(&luaELRSversion);
}

static int event()
{
  if (connectionState > FAILURE_STATES)
  {
    return DURATION_NEVER;
  }

  luadevUpdateModelID();
  setLuaTextSelectionValue(&luaModelMatch, (uint8_t)config.GetModelMatch());
  return DURATION_IMMEDIATELY;
}

static int timeout()
{
  luaHandleUpdateParameter();
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
