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

#define SETTING_RELAYCLOSE "CURTAIN_RELAYCLOSE"
#define SETTING_MAXPOSITION "CURTAIN_MAXPOSITION"
#define SETTING_INITPOSITION "CURTAIN_INITPOSITION"
#define SETTING_SPEEDTHRESHOLD "CURTAIN_SPEEDTHRESHOLD"

Encoder *_encoder;
int32_t _current_position_abs;
int8_t _current_position_prc;
int32_t _SPEED_THRESHOLD = 5;
int32_t _MAX_POSITION;
int32_t _INIT_POSITION = 0;
uint8_t _RELAY_CLOSE = 0xFF;
uint8_t _curtain_state = CURTAIN_STATE_IDLE;
uint8_t _curtain_target_relay = 0xFF;
int32_t _curtain_target_position = 0;
uint32_t _curtain_start_time = 0;

#if TERMINAL_SUPPORT

void _curtainInitCommands()
{

    settingsRegisterCommand(F("CURTAIN.SP"), [](Embedis *e) {
        _encoder->write(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.EP"), [](Embedis *e) {
        _MAX_POSITION = _encoder->read();
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

    settingsRegisterCommand(F("CURTAIN.CLOSE"), [](Embedis *e) {
        curtainOperation(100);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    settingsRegisterCommand(F("CURTAIN.OPEN"), [](Embedis *e) {
        curtainOperation(0);
        DEBUG_MSG_P(PSTR("+OK\n"));
    });

    // settingsRegisterCommand(F("CURTAIN.SPEEDTHRESHOLD"), [](Embedis *e) {
    //     _SPEED_THRESHOLD = 0;
    //     DEBUG_MSG_P(PSTR("+OK\n"));
    // });

    settingsRegisterCommand(F("CURTAIN.REPORT"), [](Embedis *e) {
        DEBUG_MSG_P(PSTR("Current position: %d\n"), _encoder->read());
        DEBUG_MSG_P(PSTR("End position: %d\n"), _MAX_POSITION);
        DEBUG_MSG_P(PSTR("Close relay: %d\n"), _RELAY_CLOSE);
        DEBUG_MSG_P(PSTR("Speed threshold: %d\n"), _SPEED_THRESHOLD);
        DEBUG_MSG_P(PSTR("Current state: %d\n"), _curtain_state);
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

    if (!hasSetting(SETTING_RELAYCLOSE))
    {
        DEBUG_MSG_P(PSTR("must learn direction at first!\n"));
        return;
        // curtainLearnDirection();
    }

    _curtain_target_position = _MAX_POSITION * pos_percent / 100;
    // if same direction as max position, then it's the close relay
    _curtain_target_relay = (_curtain_target_position - _encoder->read())*_MAX_POSITION > 0 ? _RELAY_CLOSE : 1 - _RELAY_CLOSE;
    // convert to absolute value for easier understanding
    _curtain_target_position = abs(_curtain_target_position);
    _curtain_state = CURTAIN_STATE_RUN;
    _curtain_start_time = millis();
}

//------------------------------------------------------------------------------
// MQTT
//------------------------------------------------------------------------------

#if MQTT_SUPPORT

void curtainMQTT()
{
    int8_t current_pos_prc = abs(_encoder->read() * 100 / _MAX_POSITION);
    // if (100 - current_pos_prc < 5)
    // {
    //     mqttSend(MQTT_TOPIC_CURTAIN_STATUS, "closed");
    // }
    // else
    // {
    //     mqttSend(MQTT_TOPIC_CURTAIN_STATUS, "open");
    // }
    current_pos_prc = _encoder->read() * _MAX_POSITION > 0 ? current_pos_prc : 0;
    current_pos_prc = current_pos_prc > 100 ? 100 : current_pos_prc;
    mqttSend(MQTT_TOPIC_CURTAIN_STATUS, String(current_pos_prc).c_str());
    // char buffer[4];
    // snprintf_P(buffer, sizeof(buffer), PSTR("%d"), current_pos_prc);
    // mqttSend(MQTT_TOPIC_CURTAIN_STATUS, buffer);
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
        mqttSubscribe(MQTT_TOPIC_CURTAIN_CMD);
        mqttSubscribe(MQTT_TOPIC_CURTAIN_SET_POS);
    }

    if (type == MQTT_MESSAGE_EVENT)
    {
        String t = mqttMagnitude((char *)topic);
        DEBUG_MSG_P(PSTR("topic: %s\n"), topic);
        DEBUG_MSG_P(PSTR("payload: %s\n"), payload);

        if (t.startsWith(MQTT_TOPIC_CURTAIN_CMD))
        {
            if (strcmp(payload, "OPEN") == 0) {
                curtainOperation(0);
            }
            else if (strcmp(payload, "CLOSE") == 0) {
                curtainOperation(100);
            }
            else if (strcmp(payload, "STOP") == 0) {
                _curtain_state = CURTAIN_STATE_IDLE;
                _curtain_target_relay = 0xFF;
                setSetting(SETTING_INITPOSITION, _encoder->read());
            }
            return;
        }

        // magnitude is curtain/#
        if (t.startsWith(MQTT_TOPIC_CURTAIN_SET_POS))
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

    if (hasSetting(SETTING_RELAYCLOSE))
    {
        _RELAY_CLOSE = getSetting(SETTING_RELAYCLOSE).toInt();
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

    if (hasSetting(SETTING_SPEEDTHRESHOLD))
    {
        _SPEED_THRESHOLD = getSetting(SETTING_SPEEDTHRESHOLD).toInt();
    }

#if TERMINAL_SUPPORT
    _curtainInitCommands();
#endif
#if MQTT_SUPPORT
    curtainSetupMQTT();
#endif
    relayStatus(0, false, false, false);
    relayStatus(1, false, false, false);
    espurnaRegisterLoop(curtainLoop);
}

void curtainLoop()
{
    static unsigned long last_hbeat = 0;
    if (millis() - last_hbeat > 600000) {
        last_hbeat = millis();
        curtainMQTT();
    }

    if (_curtain_state == CURTAIN_STATE_LEARN)
    {
        static int32_t pos0, pos1;
        if (millis() < _curtain_start_time + 1000)
        {
            relayStatus(0, false, false, false);
            pos0 = _encoder->read();
            return;
        }
        if (millis() < _curtain_start_time + 2000)
        {
            relayStatus(0, true, false, false);
            pos1 = _encoder->read();
            return;
        }
        relayStatus(0, false, false, false);
        _RELAY_CLOSE = abs(pos0) < abs(pos1) ? 0 : 1;
        DEBUG_MSG_P(PSTR("learning finished: close relay is %d\n"), _RELAY_CLOSE);
        setSetting(SETTING_INITPOSITION, _RELAY_CLOSE);
        _curtain_state = CURTAIN_STATE_IDLE;

        _SPEED_THRESHOLD = abs(pos1 - pos0) * 60 / 100;
        setSetting(SETTING_SPEEDTHRESHOLD, _SPEED_THRESHOLD);
        curtainMQTT();
    }
    else if (_curtain_state == CURTAIN_STATE_RUN)
    {       
        //todo: optimze stop detection
        static uint32_t last_read = 0;
        static int32_t last_pos = 0;
        if (millis() - last_read > 1000)
        {
            int32_t current_pos = _encoder->read();
            last_read = millis();
            if (abs(current_pos - last_pos) <= _SPEED_THRESHOLD)
            {
                relayStatus(_curtain_target_relay, false, false, false);
                _curtain_state = CURTAIN_STATE_IDLE;
                _curtain_target_relay = 0xFF;
                setSetting(SETTING_INITPOSITION, _encoder->read());
                curtainMQTT();
            }
            last_pos = _encoder->read();
        }

        // 5  0 -20 -200(max position), _curtain_target_position is abs value
        int8_t sign = (_MAX_POSITION > 0) - (_MAX_POSITION < 0);
        bool unfinished = _curtain_target_relay == _RELAY_CLOSE ? 
            (_encoder->read()*sign < _curtain_target_position) :
            (_encoder->read()*sign > _curtain_target_position);
        
        if (unfinished)
        {
            relayStatus(_curtain_target_relay, true, false, false);
            return;
        }
        relayStatus(_curtain_target_relay, false, false, false);
        _curtain_state = CURTAIN_STATE_IDLE;
        _curtain_target_relay = 0xFF;
        setSetting(SETTING_INITPOSITION, _encoder->read());
        curtainMQTT();
    }
    else{
        relayStatus(0, false, false, false);
        relayStatus(1, false, false, false);

        static uint32_t last_read_idle = 0;
        static int32_t last_pos_idle = 0;
        if (millis() - last_read_idle > 3000){
            uint32_t move = _encoder->read() - last_pos_idle;
            if (abs(move) > 20) {                
                if (move * _MAX_POSITION > 0) {
                    curtainOperation(100);
                }
                else {
                    curtainOperation(0);
                }              
            }
            last_pos_idle = _encoder->read();
            last_read_idle = millis();            
        }
    }
}

#endif
