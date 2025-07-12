#include <Arduino.h>

#define MY_SPLASH_SCREEN_DISABLED

//#define MY_DEBUG
#define MY_RADIO_RF24
#define MY_REPEATER_FEATURE
#define MY_RX_MESSAGE_BUFFER_FEATURE
#define MY_RX_MESSAGE_BUFFER_SIZE (10)
#define MY_RF24_IRQ_PIN 2

#define MY_NODE_ID 5
#define CHILD_ID 1              // Id of the sensor child

#define SLEEP_MODE false

#define SKETCH_NAME "Energy Meter SOLAR"
#define SKETCH_VERSION "1.0"

#include <MySensors.h>
#include <LibPrintf.h>
 
#define DIGITAL_INPUT_SENSOR 3   // The digital input you attached your light sensor.  (Only 2 and 3 generates interrupt!)
#define PULSE_FACTOR 1000        // Number of blinks per kWh of your meter. Normally 1000.
#define MAX_WATT 10000           // Max watt value to report. This filters outliers.

uint32_t SEND_FREQUENCY = 5000; // Minimum time between send (in milliseconds). We don't want to spam the gateway.
double ppwh = ((double)PULSE_FACTOR) / 1000; // Pulses per watt hour
bool pcReceived = false;
volatile uint32_t pulseCount = 0;
volatile uint32_t lastBlinkmillis = 0;
volatile uint32_t watt = 0;
uint32_t oldPulseCount = 0;
uint32_t oldWatt = 0;
double oldkWh;
uint32_t lastSend;
MyMessage wattMsg(CHILD_ID, V_WATT);
MyMessage kWhMsg(CHILD_ID, V_KWH);
MyMessage pcMsg(CHILD_ID, V_VAR1);

#define IRQ_HANDLER_ATTR

void IRQ_HANDLER_ATTR onPulse()
{
    uint32_t newBlinkmillis = millis();
    uint32_t intervalmillis = 0;

    //Serial.print("x");

    // millis will loop over and start again from 0 at 2^32
    if (newBlinkmillis > lastBlinkmillis){
        intervalmillis = newBlinkmillis - lastBlinkmillis;
    } else {
        intervalmillis = (4294967295 - lastBlinkmillis) + newBlinkmillis;
    }
  
    watt = (3600000.0 / intervalmillis) / ppwh;
    lastBlinkmillis = newBlinkmillis;

    pulseCount++;
}

void setup()
{
    Serial.begin(MY_BAUD_RATE);
    Serial.println(SKETCH_NAME);

    // This is needed for a first run to initialise messages, otherwise Home Assistant can't see the sensors
    // then comment out and upload without 
    // send(kWhMsg.set(oldkWh, 4));
    // send(wattMsg.set(0));
    // send(pcMsg.set(0));

    // Fetch last known pulse count value from gw
    request(CHILD_ID, V_VAR1);

    // Use the internal pullup to be able to hook up this sketch directly to an energy meter with S0 output
    // If no pullup is used, the reported usage will be too high because of the floating pin
    pinMode(DIGITAL_INPUT_SENSOR, INPUT_PULLUP);

    // RISING for normally off LEDs that light up for a pulse
    // FALLING for normally lit LEDs that turn off for a pulse
    attachInterrupt(digitalPinToInterrupt(DIGITAL_INPUT_SENSOR), onPulse, FALLING);
    lastSend = millis();
}

void presentation()
{
    // Send the sketch version information to the gateway and Controller
    sendSketchInfo(SKETCH_NAME, SKETCH_VERSION);

    // Register this device as power sensor
    present(CHILD_ID, S_POWER);
}

void loop()
{
    uint32_t now = millis();
    // Only send values at a maximum frequency or woken up from sleep
    bool sendTime = now - lastSend > SEND_FREQUENCY;
    if (pcReceived && (SLEEP_MODE || sendTime)) {
        // New watt value has been calculated
        if (!SLEEP_MODE && watt != oldWatt) {
            // Check that we don't get unreasonable large watt value, which
            // could happen when long wraps or false interrupt triggered
            if (watt < ((uint32_t)MAX_WATT)) {
                send(wattMsg.set(watt));  // Send watt value to gw
            }
            Serial.print("W: ");
            Serial.println(watt);
            oldWatt = watt;
        }

        // Pulse count value has changed
        if (pulseCount != oldPulseCount) {
            send(pcMsg.set(pulseCount));  // Send pulse count value to gw
            double kWh = ((double)pulseCount / ((double)PULSE_FACTOR));
            oldPulseCount = pulseCount;
            if (kWh != oldkWh) {
                send(kWhMsg.set(kWh, 4));  // Send kWh value to gw
                oldkWh = kWh;
            }
        }
        lastSend = now;
    } else if (sendTime && !pcReceived) {
        // No pulse count value received from controller. Try requesting it again.
        request(CHILD_ID, V_VAR1);
        lastSend = now;
    }

    if (SLEEP_MODE) {
        sleep(SEND_FREQUENCY, false);
    }
}

void receive(const MyMessage &message)
{
    if (message.getType()==V_VAR1) {
        pulseCount = oldPulseCount = message.getLong();
        Serial.print("RCV PULSE CNT from GW: ");
        Serial.println(pulseCount);
        pcReceived = true;
    }
}
