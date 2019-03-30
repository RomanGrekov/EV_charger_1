#include <Arduino.h>
#include <HardwareSerial.h>
#include <PZEM004T.h>
#include <dialog.h>
#include <Logger.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>
#include "images.h"
#include "FreeRTOS.h"
#include <TimeLib.h>
#include "RTClib.h"
#include <Pushbutton.h>

// Declaration using Adafruit lib
//#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h>
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
//#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
//#define SCREEN_WIDTH 128 // OLED display width, in pixels
//#define SCREEN_HEIGHT 64 // OLED display height, in pixels
//#define FONT_SIZE 16
//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#define BTN_1_PIN 32

//V, A, Wh
float Voltage=0;
float Current=0;
float Wh=0;

void init_builtin_led();

void taskControlLed( void * parameter );
void taskDisplayUpdate( void * parameter );
void taskPZEM( void * parameter );
void taskReadRTC( void * parameter );
void taskBtn1Read( void * parameter );
void taskCheckProgress( void * parameter );
String prepare_time(int val);

// Declaration display using ThingPulse lib
#define OLED_ADDR 0x3C

RTC_DS1307 rtc;

void msOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
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
OverlayCallback overlays[] = { msOverlay, footer };
int overlaysCount = 2;

int progress = 0;
String State = "Charging";
int max_charge_time = 15400;
int remain = 0; // in seconds

Pushbutton button1(BTN_1_PIN);

void setup() {
    WireMutex = xSemaphoreCreateMutex();

    // Init local LED pin
    init_builtin_led();

    // Debug serial setup
	Serial.begin(9600);

    // Setup logger
    //Logger::setLogLevel(Logger::VERBOSE);
    Logger::setLogLevel(Logger::NOTICE);

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
                10000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskCheckProgress,     // Task function.
                "TaskCheckProgress",  // String with name of task.
                10000,           // Stack size in bytes.
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
                Logger::notice("Btn pressed!");
                ui.nextFrame();
            }
        }
        vTaskDelay(10);
    }
}


void msOverlay(OLEDDisplay *display, OLEDDisplayUiState* state){
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(0, 0, State);
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(117, 0, String(Voltage));
    display->drawString(128, 0, "V");
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
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0 + x, 0 + y, "V ");
  display->drawString(10 + x, 0 + y, String(Voltage));
  display->drawString(0 + x, 20 + y, "A ");
  display->drawString(10 + x, 20 + y, String(Current));
  display->drawString(0 + x, 40 + y, "L ");
  display->drawString(10 + x, 40 + y, String(Wh));
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
    Logger::notice("OLED", "Setup UI...");
    if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
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
    else Logger::error("OLED", "Failed to get lock");

    while(true){
        if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
            int remainingTimeBudget = ui.update();
            xSemaphoreGive(WireMutex);
            if (remainingTimeBudget > 0) {
                vTaskDelay(remainingTimeBudget);
            }
        }
        else Logger::error("OLED", "Failed to get lock");
    }
}

void taskReadRTC( void * parameter ){
    char text[50];
    Logger::notice("RTC", "Initialize...");
    if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
        //Not needed. Wire will be started by OLED
        rtc.begin();
        Logger::notice("RTC", "Check if running...");
        if (! rtc.isrunning()) {
            Logger::error("RTC", "RTC is NOT running!");
            // following line sets the RTC to the date & time this sketch was compiled
            Logger::notice("RTC", "Adjust time to build time");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            Logger::notice("RTC", "OK");
            // This line sets the RTC with an explicit date & time, for example to set
            // January 21, 2014 at 3am you would call:
            // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
        }
        xSemaphoreGive(WireMutex);
    }
    else Logger::error("RTC", "Failed to get lock");

    while(true){
        if (xSemaphoreTake(WireMutex, portMAX_DELAY) == pdTRUE){
            DateTime now = rtc.now();
            setTime(now.hour(), now.minute(), now.second(), now.day(),
                    now.month(), now.year());
            //Logger::notice("Hour", itoa(now.hour(), text, 10));
            String time_str = "Current RTC Time: ";
            time_str = time_str + prepare_time(now.hour());
            time_str = time_str + ":";
            time_str = time_str + prepare_time(now.minute());
            time_str = time_str + ":";
            time_str = time_str + prepare_time(now.second());
            time_str.toCharArray(text, 50);
            Logger::notice("RTC", text);
            vTaskResume(OLEDHandle);

            xSemaphoreGive(WireMutex);
            vTaskDelete(NULL);
        }
        vTaskDelay(5000);
    }
}

void taskPZEM( void * parameter ){
    const int t_delay = 100;
    float val=0;
    char text[10];

    HardwareSerial PzemSerial2(2);     // Use hwserial UART2 at pins IO-16 (RX2) and IO-17 (TX2)
    PZEM004T pzem(&PzemSerial2);
    IPAddress ip(192,168,1,1);

    Logger::notice("pzem_init", "Connecting to PZEM...");
    while (true) {
        Logger::notice("pzem_init", ".");
        if(pzem.setAddress(ip)){
            Logger::notice("pzem_init", "Ok");
            break;
        }
        vTaskDelay(t_delay);
    }

    while(true){
        val = pzem.voltage(ip);
        if(val < 0.0) Voltage = 0.0;
        else Voltage = val;
        Logger::verbose("pzem_read", "Voltage");
        dtostrf(Voltage, 8, 2, text);
        Logger::verbose("pzem_read", text);

        val = pzem.current(ip);
        if(val < 0.0) Current = 0.0;
        else Current = val;
        Logger::verbose("pzem_read", "Current");
        dtostrf(Current, 8, 2, text);
        Logger::verbose("pzem_read", text);

        val = pzem.energy(ip);
        if(val < 0.0) Wh = 0.0;
        else Wh = val;
        Logger::verbose("pzem_read", "Wh");
        dtostrf(Wh, 8, 2, text);
        Logger::verbose("pzem_read", text);
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
