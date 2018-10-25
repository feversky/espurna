#ifndef CURTAIN_SUPPORT
#define CURTAIN_SUPPORT 0
#endif

#if CURTAIN_SUPPORT
#define ENCODER_USE_INTERRUPTS
#include <Encoder.h>
#define CURTAIN_PROTECT_TIME 3*1000

Encoder * _encoder;
int32_t   _current_position_abs;
int8_t    _current_position_prc;
int32_t   _MAX_POSITION;
uint8_t   _relay_open;

#if TERMINAL_SUPPORT

void _curtainInitCommands() {

    settingsRegisterCommand(F("CURTAIN.SP"), [](Embedis* e) {
        _encoder->write(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.EP"), [](Embedis* e) {
        _MAX_POSITION = _encoder->read();
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
    if (!hasSetting("maxPosition"))
    {
        DEBUG_MSG_P(PSTR("must learn max position at first!\n"));
        return;
    }
    DEBUG_MSG_P(PSTR("learning direction\n"));
    relayStatus(0, false, false, false);
    relayStatus(1, false, false, false);
    delay(3000);
    relayStatus(0, true, false, false);
    delay(1000);
    relayStatus(0, false, false, false);
    int32_t pos0 = _encoder->read();
    delay(2000);
    relayStatus(1, true, false, false);
    delay(1000);
    relayStatus(1, false, false, false);
    int32_t pos1 = _encoder->read();
    _relay_open = abs(pos0) < abs(pos1) ? 1 : 0;
    DEBUG_MSG_P(PSTR("learning finished\n"));
    setSetting("relayOpen", _relay_open);
}

void curtainOperation(uint8_t pos_percent){
    static bool running = false;
    static uint32_t last_operation_time = 0;

    if (pos_percent > 100) return;

    running = true;
    if (!hasSetting("relayOpen")) {
        curtainLearnDirection();
    }

    if (millis() - last_operation_time < CURTAIN_PROTECT_TIME) {
        delay(millis() - last_operation_time);
    }

    int32_t target_pos = abs(_MAX_POSITION * pos_percent / 100);
    int32_t initial_pos = abs(_encoder->read());
    uint8_t relay = initial_pos < target_pos ? _relay_open : 1 - _relay_open;
    bool condition = true;
    do {
        relayStatus(relay, true, false, false);
        delayMicroseconds(100);
        condition = initial_pos < target_pos ? abs(_encoder->read()) < target_pos : abs(_encoder->read()) > target_pos;
    } while (condition);
    relayStatus(relay, false, false, false);
    
    last_operation_time = millis();
    running = false;
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

}

#endif
