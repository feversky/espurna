#ifndef CURTAIN_SUPPORT
#define CURTAIN_SUPPORT 0
#endif

#if CURTAIN_SUPPORT
#define ENCODER_USE_INTERRUPTS
#include <Encoder.h>
#define CURTAIN_PROTECT_TIME 3 * 1000

#define CURTAIN_STATE_IDLE 0x01
#define CURTAIN_STATE_LEARN 0x02
#define CURTAIN_STATE_RUN 0x03

#define SETTING_RELAYOPEN "CURTAIN_RELAYOPEN"
#define SETTING_MAXPOSITION "CURTAIN_MAXPOSITION"
#define SETTING_INITPOSITION "CURTAIN_INITPOSITION"

Encoder *_encoder;
int32_t _current_position_abs;
int8_t _current_position_prc;
uint32_t _MAX_POSITION;
uint32_t _INIT_POSITION = 0;
uint8_t _RELAY_OPEN = 0xFF;
uint8_t _curtain_state = CURTAIN_STATE_IDLE;
uint32_t _curtain_target_relay = 0xFF;
uint32_t _curtain_target_position = 0;
uint32_t _curtain_start_time = 0;

#if TERMINAL_SUPPORT

void _curtainInitCommands()
{

    settingsRegisterCommand(F("CURTAIN.SP"), [](Embedis *e) {
        _encoder->write(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.EP"), [](Embedis *e) {
        _MAX_POSITION = abs(_encoder->read());
        setSetting(SETTING_MAXPOSITION, _MAX_POSITION);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.CP"), [](Embedis *e) {
        DEBUG_MSG_P(PSTR("Position: %d\n"), _encoder->read());
    });

    settingsRegisterCommand(F("CURTAIN.LEARN"), [](Embedis *e) {
        curtainLearnDirection();
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.OPEN"), [](Embedis *e) {
        curtainOperation(100);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.CLOSE"), [](Embedis *e) {
        curtainOperation(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });
}

#endif

void curtainLearnDirection()
{
    if (_curtain_state != CURTAIN_STATE_IDLE)
    {
        return;
    }
    if (!hasSetting(SETTING_MAXPOSITION))
    {
        DEBUG_MSG_P(PSTR("must learn max position at first!\n"));
        return;
    }

    DEBUG_MSG_P(PSTR("learning direction\n"));
    _curtain_state = CURTAIN_STATE_LEARN;
    _curtain_start_time = millis();
}

void curtainOperation(uint8_t pos_percent)
{
    if (_curtain_state != CURTAIN_STATE_IDLE)
    {
        return;
    }

    if (pos_percent > 100)
        return;

    if (!hasSetting(SETTING_RELAYOPEN))
    {
        DEBUG_MSG_P(PSTR("must learn direction at first!\n"));
        return;
        // curtainLearnDirection();
    }

    _curtain_target_position = _MAX_POSITION * pos_percent / 100;
    _curtain_target_relay = _encoder->read() < _curtain_target_position ? _RELAY_OPEN : 1 - _RELAY_OPEN;
    _curtain_state = CURTAIN_STATE_RUN;
    _curtain_start_time = millis();
}

//------------------------------------------------------------------------------
// MQTT
//------------------------------------------------------------------------------

#if MQTT_SUPPORT

void curtainMQTT()
{
    char buffer[4];
    snprintf_P(buffer, sizeof(buffer), PSTR("%d"), _current_position_prc);
    mqttSend(MQTT_TOPIC_CURTAIN_POS, buffer);
}

void curtainMQTTCallback(unsigned int type, const char *topic, const char *payload)
{

    if (type == MQTT_CONNECT_EVENT)
    {

        // Send status on connect
        curtainMQTT();

        // Subscribe topics
        // char curtain_topic[strlen(MQTT_TOPIC_CURTAIN) + 3];
        // snprintf_P(curtain_topic, sizeof(curtain_topic), PSTR("%s/+"), MQTT_TOPIC_CURTAIN);
        mqttSubscribe(MQTT_TOPIC_CURTAIN);
        mqttSubscribe(MQTT_TOPIC_CURTAIN_POS);
    }

    if (type == MQTT_MESSAGE_EVENT)
    {

        String t = mqttMagnitude((char *)topic);

        if (t.startsWith(MQTT_TOPIC_CURTAIN))
        {
            uint8_t value = *payload == '1' ? 100 : 0;
            curtainOperation(value);
            return;
        }

        // magnitude is curtain/#
        if (t.startsWith(MQTT_TOPIC_CURTAIN_POS))
        {
            uint8_t pos = atoi(payload);
            curtainOperation(pos);
            return;
        }
    }

    if (type == MQTT_DISCONNECT_EVENT)
    {
        // do nothing
    }
}

void curtainSetupMQTT()
{
    mqttRegister(curtainMQTTCallback);
}

#endif

void curtainSetup()
{
    _encoder = new Encoder(ENCODER1_PIN, ENCODER2_PIN);

    if (hasSetting(SETTING_RELAYOPEN))
    {
        _RELAY_OPEN = getSetting(SETTING_RELAYOPEN).toInt();
    }

    if (hasSetting(SETTING_MAXPOSITION))
    {
        _MAX_POSITION = getSetting(SETTING_MAXPOSITION).toInt();
    }

    if (hasSetting(SETTING_INITPOSITION))
    {
        _INIT_POSITION = getSetting(SETTING_INITPOSITION).toInt();
    }
    _encoder->write(_INIT_POSITION);
#if TERMINAL_SUPPORT
    _curtainInitCommands();
#endif
#if MQTT_SUPPORT
    curtainSetupMQTT();
#endif

    espurnaRegisterLoop(curtainLoop);
}

void curtainLoop()
{
    if (_curtain_state == CURTAIN_STATE_LEARN)
    {
        static int32_t pos0, pos1;
        if (millis() < _curtain_start_time + 3000)
        {
            relayStatus(0, false, false, false);
            relayStatus(1, false, false, false);
            return;
        }
        if (millis() < _curtain_start_time + 4000)
        {
            relayStatus(0, true, false, false);
            pos0 = _encoder->read();
            return;
        }
        if (millis() < _curtain_start_time + 7000)
        {
            relayStatus(0, false, false, false);
            return;
        }
        if (millis() < _curtain_start_time + 8000)
        {
            relayStatus(1, true, false, false);
            pos1 = _encoder->read();
            return;
        }
        relayStatus(1, false, false, false);
        _RELAY_OPEN = abs(pos0) < abs(pos1) ? 1 : 0;
        DEBUG_MSG_P(PSTR("learning finished: open relay is %d\n"), _RELAY_OPEN);
        setSetting(SETTING_RELAYOPEN, _RELAY_OPEN);
        _curtain_state = CURTAIN_STATE_IDLE;
    }
    else if (_curtain_state == CURTAIN_STATE_RUN)
    {
        bool unfinished = _curtain_target_relay == _RELAY_OPEN ? 
            (abs(_encoder->read()) < _curtain_target_position) :
            (abs(_encoder->read()) > _curtain_target_position);
        if (unfinished)
        {
            relayStatus(_curtain_target_relay, true, false, false);
            return;
        }
        relayStatus(_curtain_target_relay, false, false, false);
        _curtain_state = CURTAIN_STATE_IDLE;
        _curtain_target_relay = 0xFF;
        setSetting(SETTING_RELAYOPEN, abs(_encoder->read()));
    }
}

#endif
