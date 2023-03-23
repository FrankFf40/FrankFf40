//-----------------------------------------------------------------------------
// 2023 Ahoy, https://ahoydtu.de
// Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//-----------------------------------------------------------------------------

#ifndef __HMS_PAYLOAD_H__
#define __HMS_PAYLOAD_H__

#include "../utils/dbg.h"
#include "../utils/crc.h"
#include "../config/config.h"
#include <Arduino.h>

#define HMS_TIMEOUT_MS  30000 // 30s * 1000

typedef struct {
    uint8_t txCmd;
    uint8_t txId;
    //uint8_t invId;
    uint32_t ts;
    uint8_t data[MAX_PAYLOAD_ENTRIES][MAX_RF_PAYLOAD_SIZE];
    uint8_t len[MAX_PAYLOAD_ENTRIES];
    bool complete;
    uint8_t maxPackId;
    bool lastFound;
    uint8_t retransmits;
    bool requested;
    bool gotFragment;
} hmsPayload_t;


typedef std::function<void(uint8_t)> payloadListenerType;
typedef std::function<void(uint16_t alarmCode, uint32_t start, uint32_t end)> alarmListenerType;


template<class HMSYSTEM, class RADIO>
class HmsPayload {
    public:
        HmsPayload() {}

        void setup(IApp *app, HMSYSTEM *sys, RADIO *radio, statistics_t *stat, uint8_t maxRetransmits, uint32_t *timestamp) {
            mApp        = app;
            mSys        = sys;
            mRadio      = radio;
            mStat       = stat;
            mMaxRetrans = maxRetransmits;
            mTimestamp  = timestamp;
            for(uint8_t i = 0; i < MAX_NUM_INVERTERS; i++) {
                reset(i);
            }
            mSerialDebug = false;
            //mHighPrioIv  = NULL;
            mCbAlarm     = NULL;
            mCbPayload   = NULL;
            mLastRx      = 0;
        }

        void enableSerialDebug(bool enable) {
            mSerialDebug = enable;
        }

        void addPayloadListener(payloadListenerType cb) {
            mCbPayload = cb;
        }

        void addAlarmListener(alarmListenerType cb) {
            mCbAlarm = cb;
        }

        void loop() {
            /*if(NULL != mHighPrioIv) {
                ivSend(mHighPrioIv, true);
                mHighPrioIv = NULL;
            }*/
        }

        void ivSendHighPrio(Inverter<> *iv) {
            //mHighPrioIv = iv;
        }

        void ivSend(Inverter<> *iv, bool highPrio = false) {
            if (IV_HMS != iv->ivGen) // only process HMS inverters
                return;

            //if(!highPrio) {
                if (mPayload[iv->id].requested) {
                    if (!mPayload[iv->id].complete)
                        process(false); // no retransmit

                    if (!mPayload[iv->id].complete) {
                        if (MAX_PAYLOAD_ENTRIES == mPayload[iv->id].maxPackId)
                            mStat->rxFailNoAnser++; // got nothing
                        else
                            mStat->rxFail++; // got fragments but not complete response

                        iv->setQueuedCmdFinished();  // command failed
                        if (mSerialDebug)
                            DPRINTLN(DBG_INFO, F("enqueued cmd failed/timeout"));
                        /*if (mSerialDebug) {
                            DPRINT(DBG_INFO, F("(#"));
                            DBGPRINT(String(iv->id));
                            DBGPRINT(F(") no Payload received! (retransmits: "));
                            DBGPRINT(String(mPayload[iv->id].retransmits));
                            DBGPRINTLN(F(")"));
                        }*/
                    }
                }
            //}

            reset(iv->id);
            mPayload[iv->id].requested = true;

            yield();
            if (mSerialDebug) {
                DPRINT(DBG_INFO, F("(#"));
                DBGPRINT(String(iv->id));
                DBGPRINT(F(") Requesting Inv SN "));
                DBGPRINTLN(String(iv->config->serial.u64, HEX));
            }

            /*if (iv->getDevControlRequest()) {
                if (mSerialDebug) {
                    DPRINT(DBG_INFO, F("(#"));
                    DBGPRINT(String(iv->id));
                    DBGPRINT(F(") Devcontrol request 0x"));
                    DBGPRINT(String(iv->devControlCmd, HEX));
                    DBGPRINT(F(" power limit "));
                    DBGPRINTLN(String(iv->powerLimit[0]));
                }
                mRadio->sendControlPacket(iv->radioId.u64, iv->devControlCmd, iv->powerLimit, false);
                mPayload[iv->id].txCmd = iv->devControlCmd;
                //iv->clearCmdQueue();
                //iv->enqueCommand<InfoCommand>(SystemConfigPara); // read back power limit
            } else {
                uint8_t cmd = iv->getQueuedCmd();
                DPRINT(DBG_INFO, F("(#"));
                DBGPRINT(String(iv->id));
                DBGPRINT(F(") prepareDevInformCmd")); // + String(cmd, HEX));
                mRadio->prepareDevInformCmd(iv->radioId.u64, cmd, mPayload[iv->id].ts, iv->alarmMesIndex, false);
                mPayload[iv->id].txCmd = cmd;
            }*/
            DPRINT(DBG_INFO, "LastRx: ");
            DBGPRINTLN(String(mLastRx));
            if((mLastRx + HMS_TIMEOUT_MS) < millis()) {
                //mRadio->switchFrequency(&iv->radioId.u64, HOY_BOOT_FREQ_KHZ, HOY_BOOT_FREQ_KHZ + FREQ_STEP_KHZ);
                mRadio->switchFrequency(&iv->radioId.u64, HOY_BOOT_FREQ_KHZ, WORK_FREQ_KHZ);
                mLastRx = millis() - (HMS_TIMEOUT_MS / 6);
            } else {
                uint8_t cmd = iv->getQueuedCmd();
                DPRINT(DBG_INFO, F("(#"));
                DBGPRINT(String(iv->id));
                DBGPRINT(F(") prepareDevInformCmd")); // + String(cmd, HEX));
                mRadio->prepareDevInformCmd(&iv->radioId.u64, cmd, mPayload[iv->id].ts, iv->alarmMesIndex, false);
                mPayload[iv->id].txCmd = cmd;
            }
        }

        void add(Inverter<> *iv, hmsPacket_t *p) {
            mLastRx = millis();
            if (p->data[1] == (TX_REQ_INFO + ALL_FRAMES)) {  // response from get information command
                mPayload[iv->id].txId = p->data[1];
                DPRINTLN(DBG_DEBUG, F("Response from info request received"));
                uint8_t *pid = &p->data[10];
                if (*pid == 0x00) {
                    DPRINT(DBG_DEBUG, F("fragment number zero received and ignored"));
                } else {
                    DPRINTLN(DBG_DEBUG, "PID: 0x" + String(*pid, HEX));
                    if ((*pid & 0x7F) < MAX_PAYLOAD_ENTRIES) {
                        memcpy(mPayload[iv->id].data[(*pid & 0x7F) - 1], &p->data[11], p->data[0] - 11);
                        mPayload[iv->id].len[(*pid & 0x7F) - 1] = p->data[0] -11;
                        mPayload[iv->id].gotFragment = true;
                    }

                    if ((*pid & ALL_FRAMES) == ALL_FRAMES) {
                        // Last packet
                        if (((*pid & 0x7f) > mPayload[iv->id].maxPackId) || (MAX_PAYLOAD_ENTRIES == mPayload[iv->id].maxPackId)) {
                            mPayload[iv->id].maxPackId = (*pid & 0x7f);
                            if (*pid > 0x81)
                                mPayload[iv->id].lastFound = true;
                        }
                    }
                }
            } /*else if (p->packet[0] == (TX_REQ_DEVCONTROL + ALL_FRAMES)) { // response from dev control command
                DPRINTLN(DBG_DEBUG, F("Response from devcontrol request received"));

                mPayload[iv->id].txId = p->packet[0];
                iv->clearDevControlRequest();

                if ((p->packet[12] == ActivePowerContr) && (p->packet[13] == 0x00)) {
                    bool ok = true;
                    if((p->packet[10] == 0x00) && (p->packet[11] == 0x00))
                        mApp->setMqttPowerLimitAck(iv);
                    else
                        ok = false;
                    DPRINT(DBG_INFO, F("(#"));
                    DBGPRINT(String(iv->id));
                    DBGPRINT(F(" has "));
                    if(!ok) DBGPRINT(F("not "));
                    DBGPRINT(F("accepted power limit set point "));
                    DBGPRINT(String(iv->powerLimit[0]));
                    DBGPRINT(F(" with PowerLimitControl "));
                    DBGPRINT(String(iv->powerLimit[1]));

                    iv->clearCmdQueue();
                    iv->enqueCommand<InfoCommand>(SystemConfigPara); // read back power limit
                }
                iv->devControlCmd = Init;
            }*/
        }

        void process(bool retransmit) {
            for (uint8_t id = 0; id < mSys->getNumInverters(); id++) {
                Inverter<> *iv = mSys->getInverterByPos(id);
                if (NULL == iv)
                    continue; // skip to next inverter

                if (IV_HMS != iv->ivGen) // only process HMS inverters
                    continue; // skip to next inverter

                if ((mPayload[iv->id].txId != (TX_REQ_INFO + ALL_FRAMES)) && (0 != mPayload[iv->id].txId)) {
                    // no processing needed if txId is not 0x95
                    mPayload[iv->id].complete = true;
                    continue; // skip to next inverter
                }

                if (!mPayload[iv->id].complete) {
                    bool crcPass, pyldComplete;
                    crcPass = build(iv->id, &pyldComplete);
                    if (!crcPass && !pyldComplete) { // payload not complete
                        if ((mPayload[iv->id].requested) && (retransmit)) {
                            if (mPayload[iv->id].retransmits < mMaxRetrans) {
                                mPayload[iv->id].retransmits++;
                                if (iv->devControlCmd == Restart || iv->devControlCmd == CleanState_LockAndAlarm) {
                                    // This is required to prevent retransmissions without answer.
                                    DPRINTLN(DBG_INFO, F("Prevent retransmit on Restart / CleanState_LockAndAlarm..."));
                                    mPayload[iv->id].retransmits = mMaxRetrans;
                                } /*else if(iv->devControlCmd == ActivePowerContr) {
                                    DPRINTLN(DBG_INFO, F("retransmit power limit"));
                                    mRadio->sendControlPacket(iv->radioId.u64, iv->devControlCmd, iv->powerLimit, true);
                                }*/ else {
                                    if(false == mPayload[iv->id].gotFragment) {

                                        //DPRINTLN(DBG_WARN, F("nothing received: Request Complete Retransmit"));
                                        //mPayload[iv->id].txCmd = iv->getQueuedCmd();
                                        //DPRINTLN(DBG_INFO, F("(#") + String(iv->id) + F(") prepareDevInformCmd 0x") + String(mPayload[iv->id].txCmd, HEX));
                                        //mRadio->prepareDevInformCmd(iv->radioId.u64, mPayload[iv->id].txCmd, mPayload[iv->id].ts, iv->alarmMesIndex, true);

                                        DPRINT(DBG_INFO, F("(#"));
                                        DBGPRINT(String(iv->id));
                                        DBGPRINTLN(F(") nothing received"));
                                        mPayload[iv->id].retransmits = mMaxRetrans;
                                    } else {
                                        for (uint8_t i = 0; i < (mPayload[iv->id].maxPackId - 1); i++) {
                                            if (mPayload[iv->id].len[i] == 0) {
                                                DPRINT(DBG_WARN, F("Frame "));
                                                DBGPRINT(String(i + 1));
                                                DBGPRINTLN(F(" missing: Request Retransmit"));
                                                //mRadio->sendCmdPacket(iv->radioId.u64, TX_REQ_INFO, (SINGLE_FRAME + i), true);
                                                break;  // only request retransmit one frame per loop
                                            }
                                            yield();
                                        }
                                    }
                                }
                            }
                        }
                    } /*else if(!crcPass && pyldComplete) { // crc error on complete Payload
                        if (mPayload[iv->id].retransmits < mMaxRetrans) {
                            mPayload[iv->id].retransmits++;
                            DPRINTLN(DBG_WARN, F("CRC Error: Request Complete Retransmit"));
                            mPayload[iv->id].txCmd = iv->getQueuedCmd();
                            DPRINT(DBG_INFO, F("(#"));
                            DBGPRINT(String(iv->id));
                            DBGPRINT(F(") prepareDevInformCmd 0x"));
                            DBGPRINTLN(String(mPayload[iv->id].txCmd, HEX));
                            mRadio->prepareDevInformCmd(iv->radioId.u64, mPayload[iv->id].txCmd, mPayload[iv->id].ts, iv->alarmMesIndex, true);
                        }
                    }*/ else {  // payload complete
                        DPRINT(DBG_INFO, F("procPyld: cmd:  0x"));
                        DBGPRINTLN(String(mPayload[iv->id].txCmd, HEX));
                        DPRINT(DBG_INFO, F("procPyld: txid: 0x"));
                        DBGPRINTLN(String(mPayload[iv->id].txId, HEX));
                        DPRINTLN(DBG_DEBUG, F("procPyld: max:  ") + String(mPayload[iv->id].maxPackId));
                        record_t<> *rec = iv->getRecordStruct(mPayload[iv->id].txCmd);  // choose the parser
                        mPayload[iv->id].complete = true;

                        uint8_t payload[100];
                        uint8_t payloadLen = 0;

                        memset(payload, 0, 100);

                        for (uint8_t i = 0; i < (mPayload[iv->id].maxPackId); i++) {
                            memcpy(&payload[payloadLen], mPayload[iv->id].data[i], (mPayload[iv->id].len[i]));
                            payloadLen += (mPayload[iv->id].len[i]);
                            yield();
                        }
                        payloadLen -= 2;

                        if (mSerialDebug) {
                            DPRINT(DBG_INFO, F("Payload ("));
                            DBGPRINT(String(payloadLen));
                            DBGPRINT(F("): "));
                            ah::dumpBuf(payload, payloadLen);
                        }

                        if (NULL == rec) {
                            DPRINTLN(DBG_ERROR, F("record is NULL!"));
                        } else if ((rec->pyldLen == payloadLen) || (0 == rec->pyldLen)) {
                            if (mPayload[iv->id].txId == (TX_REQ_INFO + ALL_FRAMES))
                                mStat->rxSuccess++;

                            rec->ts = mPayload[iv->id].ts;
                            for (uint8_t i = 0; i < rec->length; i++) {
                                iv->addValue(i, payload, rec);
                                yield();
                            }
                            iv->doCalculations();
                            notify(mPayload[iv->id].txCmd);

                            /*if(AlarmData == mPayload[iv->id].txCmd) {
                                uint8_t i = 0;
                                uint16_t code;
                                uint32_t start, end;
                                while(1) {
                                    code = iv->parseAlarmLog(i++, payload, payloadLen, &start, &end);
                                    if(0 == code)
                                        break;
                                    if (NULL != mCbAlarm)
                                        (mCbAlarm)(code, start, end);
                                    yield();
                                }
                            }*/
                        } else {
                            DPRINT(DBG_ERROR, F("plausibility check failed, expected "));
                            DBGPRINT(String(rec->pyldLen));
                            DBGPRINTLN(F(" bytes"));
                            mStat->rxFail++;
                        }

                        iv->setQueuedCmdFinished();
                    }
                }
                yield();
            }
        }

    private:
        void notify(uint8_t val) {
            if(NULL != mCbPayload)
                (mCbPayload)(val);
        }

        void notify(uint16_t code, uint32_t start, uint32_t endTime) {
            if (NULL != mCbAlarm)
                (mCbAlarm)(code, start, endTime);
        }

        bool build(uint8_t id, bool *complete) {
            DPRINTLN(DBG_VERBOSE, F("build"));
            uint16_t crc = 0xffff, crcRcv = 0x0000;
            if (mPayload[id].maxPackId > MAX_PAYLOAD_ENTRIES)
                mPayload[id].maxPackId = MAX_PAYLOAD_ENTRIES;

            // check if all fragments are there
            *complete = true;
            for (uint8_t i = 0; i < mPayload[id].maxPackId; i++) {
                if(mPayload[id].len[i] == 0)
                    *complete = false;
            }
            if(!*complete)
                return false;

            for (uint8_t i = 0; i < mPayload[id].maxPackId; i++) {
                if (mPayload[id].len[i] > 0) {
                    if (i == (mPayload[id].maxPackId - 1)) {
                        crc = ah::crc16(mPayload[id].data[i], mPayload[id].len[i] - 1, crc);
                        crcRcv = (mPayload[id].data[i][mPayload[id].len[i] - 2] << 8) | (mPayload[id].data[i][mPayload[id].len[i] - 1]);
                    } else
                        crc = ah::crc16(mPayload[id].data[i], mPayload[id].len[i], crc);
                }
                yield();
            }

            return (crc == crcRcv) ? true : false;
        }

        void reset(uint8_t id) {
            DPRINT(DBG_INFO, "resetPayload: id: ");
            DBGPRINTLN(String(id));
            memset(&mPayload[id], 0, sizeof(hmsPayload_t));
            //mPayload[id].txCmd       = 0;
            mPayload[id].gotFragment = false;
            //mPayload[id].retransmits = 0;
            mPayload[id].maxPackId   = MAX_PAYLOAD_ENTRIES;
            mPayload[id].lastFound   = false;
            mPayload[id].complete    = false;
            mPayload[id].requested   = false;
            mPayload[id].ts          = *mTimestamp;
        }

        IApp *mApp;
        HMSYSTEM *mSys;
        RADIO *mRadio;
        statistics_t *mStat;
        uint8_t mMaxRetrans;
        uint32_t *mTimestamp;
        uint32_t mLastRx;
        hmsPayload_t mPayload[MAX_NUM_INVERTERS];
        bool mSerialDebug;
        Inverter<> *mHighPrioIv;

        alarmListenerType mCbAlarm;
        payloadListenerType mCbPayload;
};

#endif /*__HMS_PAYLOAD_H__*/
