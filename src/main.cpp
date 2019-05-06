#include <Arduino.h>
#include <HardwareSerial.h>
#include <PZEM004T.h>
#include <dialog.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>
#include "FreeRTOS.h"
#include <TimeLib.h>
#include "RTClib.h"
#include <Pushbutton.h>
#include <ArduinoLog.h>
#include <eeprom_cli.h>
#include <confd.h>

#define BTN_1_PIN 26
#define BTN_CONNECTOR_PIN 25

#define PILOT_ADC_PIN 36
#define PILOT_PIN 32
#define RELAY_PIN 33

#define V_BAT 3.3  // Power supply voltage

#define EEPROM_ADDR 0x50

//V, A, Wh
float Voltage=0;
float Current=0;
float Wh=0;

int Duty = 0;
float PilotVoltage=0;

enum states {
    NC,
    Connected,
    Charge,
    ChargeVent,
    Error,
    Disabled
};
enum states State = Disabled;
enum states StateOld = State;

const char state_names[6][20] = {"Not_connected",
                                 "Connected_standby",
                                 "Charge",
                                 "Charge_vent",
                                 "Error",
                                 "Disabled"};

void init_builtin_led();

void taskControlLed( void * parameter );
void taskDisplayUpdate( void * parameter );
void taskPZEM( void * parameter );
void taskReadRTC( void * parameter );
void taskBtn1Read( void * parameter );
void taskBtnConnRead( void * parameter );
void taskCheckProgress( void * parameter );
void taskCharging( void * parameter );
void taskPilotAdc( void * parameter );
String prepare_time(int val);

// Declaration display using ThingPulse lib
#define OLED_ADDR 0x3C

RTC_DS1307 rtc;

void header(OLEDDisplay *display, OLEDDisplayUiState* state);
void footer(OLEDDisplay *display, OLEDDisplayUiState* state);
void drawFrame1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawFrame2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);

SemaphoreHandle_t WireMutex;
TaskHandle_t OLEDHandle;
    SSD1306Wire  display(OLED_ADDR, 21, 22);
    OLEDDisplayUi ui     ( &display );

FrameCallback frames[] = { drawFrame1, drawFrame2}; //, drawFrame3, drawFrame4, drawFrame5 };
// how many frames are there?
int frameCount = 2;
// Overlays are statically drawn on top of a frame eg. a clock
OverlayCallback overlays[] = { header, footer };
int overlaysCount = 2;

int progress = 0;
int max_charge_time = 15400;
int remain = 0; // in seconds

Pushbutton button1(BTN_1_PIN);
Pushbutton connector_button(BTN_CONNECTOR_PIN);

void setup() {
    WireMutex = xSemaphoreCreateMutex();

    // Init local LED pin
    init_builtin_led();

    // Debug serial setup
	Serial.begin(9600);

    while(!Serial && !Serial.available()){}
    Log.begin   (LOG_LEVEL_VERBOSE, &Serial);
    Log.notice("###### Start logger ######"CR);


    // log, sda, scl, address
    EepromCli eeprom(21, 22, EEPROM_ADDR);
    Confd confd(eeprom);
    
    float a, *p;
    uint8_t res;
    p = &a;
    res = confd.read_kwh(p);
    Log.notice("Data: %F"CR, a);
    res = confd.write_kwh(999999);



    xTaskCreate(taskControlLed,   /* Task function. */
                "TaskLedBlinker", /* String with name of task. */
                10000,            /* Stack size in bytes. */
                NULL,             /* Parameter passed as input of the task */
                1,                /* Priority of the task. */
                NULL);            /* Task handle. */

    xTaskCreate(taskDisplayUpdate,  /* Task function. */
                "TaskDisplayUpdate",/* String with name of task. */
                10000,            /* Stack size in bytes. */
                NULL,             /* Parameter passed as input of the task */
                1,                /* Priority of the task. */
                &OLEDHandle);            /* Task handle. */
    vTaskSuspend(OLEDHandle); // Will be resumed after RTC init and read

    xTaskCreate(taskPZEM,
                "TaskPZEMRead",
                10000,
                NULL,
                1,
                NULL);

    xTaskCreate(taskReadRTC,     // Task function.
                "TaskReadTime",  // String with name of task.
                10000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskBtn1Read,     // Task function.
                "TaskBtn1Read",  // String with name of task.
                1000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskBtnConnRead,     // Task function.
                "taskBtnConnRead",  // String with name of task.
                1000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskCheckProgress,     // Task function.
                "TaskCheckProgress",  // String with name of task.
                10000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskCharging,     // Task function.
                "TaskCharging",  // String with name of task.
                10000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskPilotAdc,     // Task function.
                "TaskPilotAdc",  // String with name of task.
                1000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.
}

void loop(){
    delay(1000);
}

void init_builtin_led(){
    pinMode(LED_BUILTIN, OUTPUT);
}

void taskBtn1Read( void * parameter ){
    bool btn_state = false;
    bool btn_state_old = btn_state;
    while(1){
        btn_state = button1.isPressed();
        if (btn_state != btn_state_old){
            btn_state_old = btn_state;
            if (btn_state == true){
                Log.notice("Btn pressed!"CR);
                ui.nextFrame();
            }
        }
        vTaskDelay(100);
    }
}

void taskBtnConnRead( void * parameter ){
    bool btn_state = false;
    bool btn_state_old = btn_state;
    while(1){
        btn_state = connector_button.isPressed();
        if (btn_state != btn_state_old){
            btn_state_old = btn_state;
            if (btn_state == true){
                Log.notice("Btn on connector pressed!"CR);
                Log.notice("Make state NC"CR);
                State = NC;
            }
        }
        vTaskDelay(100);
    }
}


void header(OLEDDisplay *display, OLEDDisplayUiState* state){
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(0, 0, state_names[State]);
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(117, 0, String(Voltage));
    display->drawString(128, 0, "V");
    display->drawLine( 0, 14, 128, 14);
}

String prepare_time(int val){
    if (val < 10){
        return "0" + String(val);
    }
    return String(val);
}

void footer(OLEDDisplay *display, OLEDDisplayUiState* state){
    display->drawLine( 0, 53, 128, 53);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    display->drawString(0, 54, prepare_time(hour()));
    display->drawString(12, 54, ":");
    display->drawString(15, 54, prepare_time(minute()));
    display->drawString(27, 54, ":");
    display->drawString(30, 54, prepare_time(second()));

    int r_h, r_m, r_s, seconds_left;
    if (remain >= 3600) r_h = remain/3600;
    else r_h = 0;
    seconds_left = remain - r_h * 3600;
    if (seconds_left > 0){
        r_m = seconds_left / 60;
        r_s = seconds_left % 60;
    }
    else{
        r_m = 0;
        r_s = 0;
    }
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(90, 54, prepare_time(r_h));
    display->drawString(102, 54, ":");
    display->drawString(105, 54, prepare_time(r_m));
    display->drawString(117, 54, ":");
    display->drawString(120, 54, prepare_time(r_s));
}

void drawFrame1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  // draw an xbm image.
  // Please note that everything that should be transitioned
  // needs to be drawn relative to x and y

  //display->drawXbm(x + 34, y + 14, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 18, String(progress));
  display->drawString(74, 18, "%");
  display->drawProgressBar(4, 32, 120, 8, progress);
}

void drawFrame2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  // Demonstrates the 3 included default sizes. The fonts come from SSD1306Fonts.h file
  // Besides the default fonts there will be a program to convert TrueType fonts into this format
  int _x=0;
  int _y=16;
  int h=10;
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(_x + x, _y + y, "V ");
  _x = _x + 10;
  display->drawString(_x + x, _y + y, String(Voltage));
  _x = _x + 50;
  display->drawString(_x + x, _y + y, "Pilot V");
  _x = _x + 40;
  display->drawString(_x + x, _y + y, String(PilotVoltage));

  _x = 0;
  _y = _y + h;
  display->drawString(_x + x, _y + y, "A ");
  _x = _x + 10;
  display->drawString(_x + x, _y + y, String(Current));

  _x = 0;
  _y = _y + h;
  display->drawString(_x + x, _y + y, "L ");
  _x = _x + 10;
  display->drawString(_x + x, _y + y, String(Wh));
}

void taskControlLed( void * parameter ){
    bool ledstatus = false;
    while(1){
        ledstatus = !ledstatus;
        digitalWrite(LED_BUILTIN, ledstatus);
        vTaskDelay(1000);
    }
}

void taskDisplayUpdate( void * parameter ){
    // ADDR, SDA, SCL pins
    Log.notice("OLED: Setup UI"CR);
    if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
        display.setI2cAutoInit(true);
        ui.setTargetFPS(60);
        // Customize the active and inactive symbol
        //ui.setActiveSymbol(activeSymbol);
        //ui.setInactiveSymbol(inactiveSymbol);
        // You can change this to
        // TOP, LEFT, BOTTOM, RIGHT
        //ui.setIndicatorPosition(BOTTOM);
        // Defines where the first frame is located in the bar.
        //ui.setIndicatorDirection(LEFT_RIGHT);
        ui.disableAllIndicators();
        // You can change the transition that is used
        // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_UP, SLIDE_DOWN
        ui.setFrameAnimation(SLIDE_LEFT);
        // Add frames
        ui.setFrames(frames, frameCount);
        // Add overlays
        ui.setOverlays(overlays, overlaysCount);
        // Initialising the UI will init the display too.

        ui.disableAutoTransition();
        ui.init();
        display.flipScreenVertically();

        xSemaphoreGive(WireMutex);
    }
    else Log.error("OLED: Failed to get lock"CR);

    while(true){
        if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
            int remainingTimeBudget = ui.update();
            xSemaphoreGive(WireMutex);
            if (remainingTimeBudget > 0) {
                vTaskDelay(remainingTimeBudget);
            }
        }
        else Log.error("OLED: Failed to get lock"CR);
    }
}

void taskReadRTC( void * parameter ){
    Log.notice("RTC: Initialize"CR);
    if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
        //Not needed. Wire will be started by OLED
        rtc.begin();
        Log.notice("RTC: Check if running..."CR);
        if (! rtc.isrunning()) {
            Log.error("RTC: RTC is NOT running!"CR);
            // following line sets the RTC to the date & time this sketch was compiled
            Log.notice("RTC: Adjust time to build time"CR);
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            Log.notice("RTC: OK"CR);
            // This line sets the RTC with an explicit date & time, for example to set
            // January 21, 2014 at 3am you would call:
            // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
        }
        xSemaphoreGive(WireMutex);
    }
    else Log.error("RTC: Failed to get lock"CR);

    while(true){
        if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
            DateTime now = rtc.now();
            setTime(now.hour(), now.minute(), now.second(), now.day(),
                    now.month(), now.year());
            Log.notice("Current RTC Time: %d:%d:%d"CR, now.hour(),
                                                       now.minute(),
                                                       now.second());
            vTaskResume(OLEDHandle);

            xSemaphoreGive(WireMutex);
            vTaskDelete(NULL);
        }
        vTaskDelay(5000);
    }
}

void taskPZEM( void * parameter ){
    const int t_delay = 1000;
    float val=0;

    HardwareSerial PzemSerial2(2);     // Use hwserial UART2 at pins IO-16 (RX2) and IO-17 (TX2)
    PZEM004T pzem(&PzemSerial2);
    IPAddress ip(192,168,1,1);

    Log.notice("pzem_init: Connecting to PZEM..."CR);
    while (true) {
        if(pzem.setAddress(ip)){
            Log.notice("pzem_init: Ok"CR);
            break;
        }
        vTaskDelay(t_delay);
    }

    while(true){
        val = pzem.voltage(ip);
        if(val < 0.0) Voltage = 0.0;
        else Voltage = val;
        Log.verbose("pzem_read: Voltage %F"CR, Voltage);

        val = pzem.current(ip);
        if(val < 0.0) Current = 0.0;
        else Current = val;
        Log.verbose("pzem_read: Current %F"CR, Current);

        val = pzem.energy(ip);
        if(val < 0.0) Wh = 0.0;
        else Wh = val;
        Log.verbose("pzem_read: Wh %F"CR, Wh);
        vTaskDelay(500);
    }
}

void taskCheckProgress( void * parameter ){
    while (1){
        if (progress < 100) progress++;
        else progress = 0;
        remain = max_charge_time - (max_charge_time/100 * progress);
        vTaskDelay(100);
    }
}

void taskCharging( void * parameter ){
    const int channel = 0;
    const int freq = 1000;
    const int resolution = 10;
    const int duty_max = 1023; // 2 to 10 degrees
    // duty is global

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, false);

    ledcSetup(channel, freq, resolution);
    ledcAttachPin(PILOT_PIN, channel);
    while (1) {
        switch (State){
            case NC:
                digitalWrite(RELAY_PIN, false);
                ledcWrite(channel, 0); // We have reverse version of duty. 0 - means maximum duty
                if (7.5 < PilotVoltage && PilotVoltage < 9.5) State = Connected;
            break;
            case Connected:
                Duty = 510; // Just for test. It should be setup somewhere else
                ledcWrite(channel, Duty);
                if (4.5 < PilotVoltage < 6.5){
                    State = Charge;
                    break;
                }
            break;
            case Charge:
                digitalWrite(RELAY_PIN, true);
                if (PilotVoltage < 1) State = Error;
                if (10.5 < PilotVoltage && PilotVoltage < 12.5) State = NC;
            break;
            case Error:
                digitalWrite(RELAY_PIN, false);
                if (10.5 < PilotVoltage && PilotVoltage < 12.5) State = NC;
            break;
            case Disabled:
                Log.notice("System is in Disabled state. Doing nothing..."CR);
                vTaskDelay(10000);
            break;
        }
        if (State != StateOld){
            StateOld = State;
            Log.notice("***state*** %s"CR, state_names[State]);
        }
        //digitalWrite(RELAY_PIN, false);
        //vTaskDelay(1000);
        //digitalWrite(RELAY_PIN, true);
        vTaskDelay(100);
    }
}

void taskPilotAdc( void * parameter ){
    const float l = 0.05; // 10%
    static float x_old = 1;
    float x = 0;
    float x_t = 0;

    while (1) {
        x = float(analogRead(PILOT_ADC_PIN)) / 4096 * V_BAT;
        x_t = l * x + (1 - l) * x_old;
        x_old = x_t;
        PilotVoltage = x_t * 4.41; // 12V / 2.72 after divider(68K/20K)
        vTaskDelay(10);
    }
}
