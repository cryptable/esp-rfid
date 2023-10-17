/*
MIT License

Copyright (c) 2018 esp-rfid Community
Copyright (c) 2017 Ömer Şiar Baysal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
#define VERSION "2.0.0"

#include "Arduino.h"
#include <WiFi.h>
#include <SPI.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TimeLib.h>
#include <Ticker.h>
#include "Ntp.h"
#include <AsyncMqttClient.h>
#include <Bounce2.h>
#include <SPIFFS.h>
#ifdef MFRC522_READER
#include <MFRC522.h>
MFRC522 mfrc522 = MFRC522();
#endif
#ifdef PN532_READER
#include "PN532.esp"
PN532 pn532;
#endif
#ifdef WIEGAND_READER
#include <Wiegand.h>
WIEGAND wg;
#endif
#ifdef RF125_READER
#include "rfid125kHz.esp"
RFID_Reader RFIDr;
#endif

int rfidss;
int readerType;
int relayPin;

// these are from vendors
// #include "webh/glyphicons-halflings-regular.woff.gz.h"
#include "webh/required.css.gz.h"
#include "webh/required.js.gz.h"

// these are from us which can be updated and changed
#include "webh/esprfid.js.gz.h"
#include "webh/esprfid.htm.gz.h"
#include "webh/index.html.gz.h"

NtpClient NTP;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Bounce button;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long 	blink_ = millis();
bool 			wifiFlag = false;
bool 			configMode = false;
int 			wmode;
uint8_t 		wifipin = 255;
uint8_t 		buttonPin = 255;
#define 		LEDoff HIGH
#define 		LEDon LOW

// Variables for whole scope
const char 		*http_username = "admin";
char 			*http_pass = NULL;
unsigned long 	previousMillis = 0;
unsigned long 	previousLoopMillis = 0;
unsigned long 	currentMillis = 0;
unsigned long 	cooldown = 0;
unsigned long 	deltaTime = 0;
unsigned long 	uptime = 0;
bool 			shouldReboot = false;
bool 			activateRelay = false;
bool 			deactivateRelay = false;
bool 			inAPMode = false;
bool 			isWifiConnected = false;
unsigned long 	autoRestartIntervalSeconds = 0;

bool 			wifiDisabled = true;
bool 			doDisableWifi = false;
bool 			doEnableWifi = false;
bool 			timerequest = false;
bool 			formatreq = false;
unsigned long 	wifiTimeout = 0;
unsigned long 	wiFiUptimeMillis = 0;
char 			*deviceHostname = NULL;

int 			mqttenabled = 0;
char 			*mqttTopic = NULL;
char 			*mhs = NULL;
char 			*muser = NULL;
char 			*mpas = NULL;
int 			mport;

int 			lockType;
int 			relayType;
unsigned long 	activateTime;
int 			timeZone;

unsigned long nextbeat = 0;
unsigned long interval = 1800;

#include "log.esp"
#include "mqtt.esp"
#include "helpers.esp"
#include "wsResponses.esp"
#include "rfid.esp"
#include "wifi.esp"
#include "config.esp"
#include "websocket.esp"
#include "webserver.esp"

#define SS_PIN		21
#define SCK_PIN		14
#define MOSI_PIN	23
#define MISO_PIN	15
#define RST_PIN		22

void ICACHE_FLASH_ATTR setup()
{

#ifdef DEBUG
	Serial.begin(9600);
	Serial.println();

	Serial.print(F("[ INFO ] ESP RFID v"));
	Serial.println(VERSION);

	uint32_t realSize = ESP.getFlashChipSize();
	uint32_t ideSize = ESP.getSketchSize();
	FlashMode_t ideMode = ESP.getFlashChipMode();
	Serial.printf("Flash real id:   %08X\n", ESP.getChipRevision());
	Serial.printf("Flash real size: %u\n\n", realSize);
	Serial.printf("Flash ide  size: %u\n", ideSize);
	Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
	Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
	if (ideSize != realSize)
	{
		Serial.println("Flash Chip configuration wrong!\n");
	}
	else
	{
		Serial.println("Flash Chip configuration ok.\n");
	}
#endif

	if (!SPIFFS.begin())
	{
#ifdef DEBUG
		Serial.print(F("[ WARN ] Formatting filesystem..."));
#endif
		if (SPIFFS.format())
		{
			writeEvent("WARN", "sys", "Filesystem formatted", "");

#ifdef DEBUG
			Serial.println(F(" completed!"));
#endif
		}
		else
		{
#ifdef DEBUG
			Serial.println(F(" failed!"));
			Serial.println(F("[ WARN ] Could not format filesystem!"));
#endif
		}
	}

	if (formatreq)
	{
#ifdef DEBUG
		Serial.println(F("[ WARN ] Factory reset initiated..."));
#endif
		SPIFFS.end();
		ws.enable(false);
		SPIFFS.format();
		ESP.restart();
	}
	
// #ifdef MFRC522
	Serial.println("Set SPI: .\n");
	SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
// #endif

	// WiFiEventId_t wifiDisconnectHandler = 
	WiFi.onEvent(onWifiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
	// WiFiEventId_t wifiConnectHandler = 
	WiFi.onEvent(onWifiConnect, ARDUINO_EVENT_WIFI_STA_CONNECTED);
	configMode = loadConfiguration();
	if (!configMode)
	{
		fallbacktoAPMode();
		configMode = false;
	}
	else {
		configMode = true;
	}
#ifdef DEBUG
	Serial.println(F("[ INFO ] Setup WebServer"));
#endif
	setupWebServer();
#ifdef DEBUG
	Serial.println(F("[ INFO ] Setup WebServer done"));
#endif
	writeEvent("INFO", "sys", "System setup completed, running", "");
}

void loop()
{
	currentMillis = millis();
	deltaTime = currentMillis - previousLoopMillis;
	uptime = NTP.getUptimeSec();
	previousLoopMillis = currentMillis;

	button.update();
	if (button.fell()) 
	{
#ifdef DEBUG
		Serial.println("Button has been pressed");
#endif
		writeLatest("", "(used open/close button)", 1);
		activateRelay = true;
	}

	if (wifipin != 255 && configMode && !wmode)
	{
		if (!wifiFlag)
		{
			if ((currentMillis - blink_) > 500)
			{
				blink_ = currentMillis;
				digitalWrite(wifipin, !digitalRead(wifipin));
			}
		}
		else
		{
			if (!(digitalRead(wifipin)==LEDon)) digitalWrite(wifipin, LEDon);
		}
	}

	if (currentMillis >= cooldown)
	{
		rfidloop();
	}

	// Continuous relay mode
	if (lockType == 1)
	{
		if (activateRelay)
		{
			// currently OFF, need to switch ON
			if (digitalRead(relayPin) == !relayType)
			{
#ifdef DEBUG
				Serial.print("mili : ");
				Serial.println(millis());
				Serial.println("activating relay now");
#endif
				digitalWrite(relayPin, relayType);
			}
			else	// currently ON, need to switch OFF
			{
#ifdef DEBUG
				Serial.print("mili : ");
				Serial.println(millis());
				Serial.println("deactivating relay now");
#endif				
				digitalWrite(relayPin, !relayType);
			}
			activateRelay = false;	
		}
	}
	else if (lockType == 0)	// momentary relay mode
	{
		if (activateRelay)
		{
#ifdef DEBUG
			Serial.print("mili : ");
			Serial.println(millis());
			Serial.println("activating relay now");
#endif
			digitalWrite(relayPin, relayType);
			previousMillis = millis();
			activateRelay = false;
			deactivateRelay = true;
		}
		else if ((currentMillis - previousMillis >= activateTime) && (deactivateRelay))
		{
#ifdef DEBUG
			Serial.println(currentMillis);
			Serial.println(previousMillis);
			Serial.println(activateTime);
			Serial.println(activateRelay);
			Serial.println("deactivate relay after this");
			Serial.print("mili : ");
			Serial.println(millis());
#endif
			digitalWrite(relayPin, !relayType);
			deactivateRelay = false;
		}
	}

	if (timerequest)
	{
		timerequest = false;
		sendTime();
	}

	if (autoRestartIntervalSeconds > 0 && uptime > autoRestartIntervalSeconds * 1000)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
#ifdef DEBUG
		Serial.println(F("[ WARN ] Auto restarting..."));
#endif
		shouldReboot = true;
	}

	if (shouldReboot)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
#ifdef DEBUG
		Serial.println(F("[ INFO ] Rebooting..."));
#endif
		ESP.restart();
	}

	if (isWifiConnected)
	{
		wiFiUptimeMillis += deltaTime;
	}

	if (wifiTimeout > 0 && wiFiUptimeMillis > (wifiTimeout * 1000) && isWifiConnected == true)
	{
		writeEvent("INFO", "wifi", "WiFi is going to be disabled", "");
		doDisableWifi = true;
	}

	if (doDisableWifi == true)
	{
		doDisableWifi = false;
		wiFiUptimeMillis = 0;
		disableWifi();
	}
	else if (doEnableWifi == true)
	{
		writeEvent("INFO", "wifi", "Enabling WiFi", "");
		doEnableWifi = false;
		if (!isWifiConnected)
		{
			wiFiUptimeMillis = 0;
			enableWifi();
		}
	}

	if (mqttenabled == 1)
	{
		if (mqttClient.connected())
		{
			if ((unsigned)now() > nextbeat)
			{
				mqtt_publish_heartbeat(now());
				nextbeat = (unsigned)now() + interval;
#ifdef DEBUG
				Serial.print("[ INFO ] Nextbeat=");
				Serial.println(nextbeat);
#endif
			}
		}
	}
}
