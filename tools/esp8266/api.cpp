//-----------------------------------------------------------------------------
// 2022 Ahoy, https://www.mikrocontroller.net/topic/525778
// Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//-----------------------------------------------------------------------------

#if defined(ESP32) && defined(F)
  #undef F
  #define F(sl) (sl)
#endif

#include "api.h"
#include "AsyncJson.h"

//-----------------------------------------------------------------------------
api::api(AsyncWebServer *srv, app *app, sysConfig_t *sysCfg, config_t *config, char version[]) {
    mSrv = srv;
    mApp = app;
    mSysCfg  = sysCfg;
    mConfig  = config;
    mVersion = version;
}


//-----------------------------------------------------------------------------
void api::setup(void) {
    mSrv->on("/api/system",        HTTP_GET, std::bind(&api::onSystem,       this, std::placeholders::_1));
    mSrv->on("/api/inverter/list", HTTP_GET, std::bind(&api::onInverterList, this, std::placeholders::_1));
    mSrv->on("/api/mqtt",          HTTP_GET, std::bind(&api::onMqtt,         this, std::placeholders::_1));
}


//-----------------------------------------------------------------------------
void api::loop(void) {

}


//-----------------------------------------------------------------------------
void api::onSystem(AsyncWebServerRequest *request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();

    root[F("ssid")] = mSysCfg->stationSsid;
    root[F("device_name")] = mSysCfg->deviceName;

    response->setLength();
    //response->addHeader("Access-Control-Allow-Origin", "*");
    //response->addHeader("Access-Control-Allow-Headers", "content-type");
    request->send(response);
}


//-----------------------------------------------------------------------------
void api::onInverterList(AsyncWebServerRequest *request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    JsonArray invArr = root.createNestedArray("inverter");

    Inverter<> *iv;
    for(uint8_t i = 0; i < MAX_NUM_INVERTERS; i ++) {
        iv = mApp->mSys->getInverterByPos(i);
        if(NULL != iv) {
            JsonObject obj = invArr.createNestedObject();
            obj[F("id")]       = i;
            obj[F("name")]     = String(iv->name);
            obj[F("serial")]   = String(iv->serial.u64, HEX);
            obj[F("channels")] = iv->channels;
            obj[F("version")]  = String(iv->fwVersion);

            for(uint8_t j = 0; j < iv->channels; j ++) {
                obj[F("ch_max_power")][j] = iv->chMaxPwr[j];
                obj[F("ch_name")][j] = iv->chName[j];
            }

            obj[F("power_limit")][F("limit")] = iv->powerLimit[0];
            obj[F("power_limit")][F("limit_option")] = iv->powerLimit[1];
        }
    }
    root[F("interval")] = String(mConfig->sendInterval);
    root[F("retries")]  = String(mConfig->maxRetransPerPyld);

    response->setLength();
    request->send(response);
}


//-----------------------------------------------------------------------------
void api::onMqtt(AsyncWebServerRequest *request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();

    root[F("broker")] = String(mConfig->mqtt.broker);
    root[F("port")]   = String(mConfig->mqtt.port);
    root[F("user")]   = String(mConfig->mqtt.user);
    root[F("pwd")]    = String(mConfig->mqtt.pwd); // TODO: not that nice!
    root[F("topic")]  = String(mConfig->mqtt.topic);

    response->setLength();
    request->send(response);
}