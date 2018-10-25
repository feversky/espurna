#ifndef CURTAIN_SUPPORT
#define CURTAIN_SUPPORT 0
#endif

#if CURTAIN_SUPPORT
#define ENCODER_USE_INTERRUPTS
#include <Encoder.h>
#define CURTAIN_PROTECT_TIME 3*1000

#define CURTAIN_STATE_IDLE  0x01
#define CURTAIN_STATE_LEARN 0x02
#define CURTAIN_STATE_RUN   0x03

Encoder * _encoder;
int32_t   _current_position_abs;
int8_t    _current_position_prc;
uint32_t  _MAX_POSITION;
uint8_t   _relay_open;
uint8_t   _curtain_state = CURTAIN_STATE_IDLE;
uint32_t  _curtain_target_position = 0;
uint32_t  _curtain_start_time = 0;


#if TERMINAL_SUPPORT

void _curtainInitCommands() {

    settingsRegisterCommand(F("CURTAIN.SP"), [](Embedis* e) {
        _encoder->write(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.EP"), [](Embedis* e) {
        _MAX_POSITION = abs(_encoder->read());
        setSetting("maxPosition", _MAX_POSITION);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.CP"), [](Embedis* e) {
        DEBUG_MSG_P(PSTR("Position: %d\n"), _encoder->read());
    });

    settingsRegisterCommand(F("CURTAIN.LEARN"), [](Embedis* e) {
        curtainLearnDirection();
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.OPEN"), [](Embedis* e) {
        curtainOperation(100);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.CLOSE"), [](Embedis* e) {
        curtainOperation(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });
}

#endif

void curtainLearnDirection(){
    if (_curtain_state != CURTAIN_STATE_IDLE)
    {
        return;
    }
    if (!hasSetting("maxPosition"))
    {
        DEBUG_MSG_P(PSTR("must learn max position at first!\n"));
        return;
    }

    DEBUG_MSG_P(PSTR("learning direction\n"));
    _curtain_state = CURTAIN_STATE_LEARN;
    _curtain_start_time = millis();
    // DEBUG_MSG_P(PSTR("learning direction\n"));
    // relayStatus(0, false, false, false);
    // relayStatus(1, false, false, false);
    // delay(3000);
    // relayStatus(0, true, false, false);
    // delay(1000);
    // relayStatus(0, false, false, false);
    // int32_t pos0 = _encoder->read();
    // delay(2000);
    // relayStatus(1, true, false, false);
    // delay(1000);
    // relayStatus(1, false, false, false);
    // int32_t pos1 = _encoder->read();
    // _relay_open = abs(pos0) < abs(pos1) ? 1 : 0;
    // DEBUG_MSG_P(PSTR("learning finished\n"));
    // setSetting("relayOpen", _relay_open);
}

void curtainOperation(uint8_t pos_percent){
    if (_curtain_state != CURTAIN_STATE_IDLE)
    {
        return;
    }

    if (pos_percent > 100) return;

    if (!hasSetting("relayOpen")) {
        DEBUG_MSG_P(PSTR("must learn direction at first!\n"));
        return;
        // curtainLearnDirection();
    }

    _curtain_target_position = _MAX_POSITION * pos_percent / 100;
    _curtain_state = CURTAIN_STATE_RUN;
    _curtain_start_time = millis();
    
}

//------------------------------------------------------------------------------
// MQTT
//------------------------------------------------------------------------------

#if MQTT_SUPPORT

void curtainMQTT() {
    char buffer[4];
    snprintf_P(buffer, sizeof(buffer), PSTR("%d"), _current_position_prc);
    mqttSend(MQTT_TOPIC_CURTAIN_POS, buffer);
}

void curtainMQTTCallback(unsigned int type, const char * topic, const char * payload) {

    if (type == MQTT_CONNECT_EVENT) {

        // Send status on connect
        curtainMQTT();

        // Subscribe topics
        // char curtain_topic[strlen(MQTT_TOPIC_CURTAIN) + 3];
        // snprintf_P(curtain_topic, sizeof(curtain_topic), PSTR("%s/+"), MQTT_TOPIC_CURTAIN);
        mqttSubscribe(MQTT_TOPIC_CURTAIN);
        mqttSubscribe(MQTT_TOPIC_CURTAIN_POS);

    }

    if (type == MQTT_MESSAGE_EVENT) {

        String t = mqttMagnitude((char *) topic);

        if (t.startsWith(MQTT_TOPIC_CURTAIN)) {
            uint8_t value = *payload == '1' ? 100 : 0;
            curtainOperation(value);
            return;
        }

        // magnitude is curtain/#
        if (t.startsWith(MQTT_TOPIC_CURTAIN_POS)) {
            uint8_t pos = atoi(payload);
            curtainOperation(pos);
            return;
        }
    }

    if (type == MQTT_DISCONNECT_EVENT) {
        // do nothing
    }

}

void curtainSetupMQTT() {
    mqttRegister(curtainMQTTCallback);
}

#endif

void curtainSetup(){
    _encoder = new Encoder(ENCODER1_PIN, ENCODER2_PIN);

    #if TERMINAL_SUPPORT
        _curtainInitCommands();
    #endif
    #if MQTT_SUPPORT
        curtainSetupMQTT();
    #endif

    espurnaRegisterLoop(curtainLoop);
}

void curtainLoop(){
    if (_curtain_state == CURTAIN_STATE_LEARN)
    {
        static int32_t pos0, pos1;
        if (millis() < _curtain_start_time + 3000 )
        {
            relayStatus(0, false, false, false);
            relayStatus(1, false, false, false);
            return;
        }
        if (millis() < _curtain_start_time + 4000 )
        {
            relayStatus(0, true, false, false);
            pos0 = _encoder->read();
            return;
        }
        if (millis() < _curtain_start_time + 7000 )
        {
            relayStatus(0, false, false, false);
            return;
        }
        if (millis() < _curtain_start_time + 8000 )
        {
            relayStatus(1, true, false, false);
            pos1 = _encoder->read();
            return;
        }
        relayStatus(1, false, false, false);
        _relay_open = abs(pos0) < abs(pos1) ? 1 : 0;
        DEBUG_MSG_P(PSTR("learning finished: open relay %d\n"), _relay_open);
        setSetting("relayOpen", _relay_open);
        _curtain_state = CURTAIN_STATE_IDLE;
    }
    else if (_curtain_state == CURTAIN_STATE_RUN)
    {
        int32_t target_pos = _curtain_target_position;
        int32_t initial_pos = abs(_encoder->read());
        uint8_t relay = initial_pos < target_pos ? _relay_open : 1 - _relay_open;
        bool condition = true;
        condition = initial_pos < target_pos ? abs(_encoder->read()) < target_pos : abs(_encoder->read()) > target_pos;

        relayStatus(relay, true, false, false);
        if (condition)
        {
            return;
        }
        relayStatus(relay, false, false, false);
        _curtain_state = CURTAIN_STATE_IDLE;        
    }
}

#endif
