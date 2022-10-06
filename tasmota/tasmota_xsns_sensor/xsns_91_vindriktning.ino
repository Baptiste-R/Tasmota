/*
  xsns_91_vindriktning.ino - IKEA vindriktning particle concentration sensor support for Tasmota

  Copyright (C) 2021  Marcel Ritter and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_VINDRIKTNING
/*********************************************************************************************\
 * IKEA VINDRIKTNING PM2.5 particle concentration sensor
 *
 * This sensor uses a subset of the PM1006K LED particle sensor
 * To use Tasmota the user needs to add an ESP8266 or ESP32
 * To update the tricolor light, user needs to proxy the PM_TX line with raw value corresponding
 * to the color wanted (0-35: Green / 36-85: Orange / 86-x: Red)
 * 
 * Console instruction supported:
 * 
 * Instruction              Returns                     Function
 * ------------------------------------------------------------------------------------
 * VindriktningColor 0..2   Corresponding PM value      Set tricolor light status (green, orangen red)
\*********************************************************************************************/

#define XSNS_91                   91

//#define VINDRIKTNING_SHOW_PM1         // Display undocumented/supposed PM1.0 values
//#define VINDRIKTNING_SHOW_PM10        // Display undocumented/supposed PM10 values

#include <TasmotaSerial.h>

#define VINDRIKTNING_DATASET_SIZE 20

#define VINDRIKTNING_CMD_GREEN    0
#define VINDRIKTNING_CMD_ORANGE   1
#define VINDRIKTNING_CMD_RED      2

#define VINDRIKTNING_COLOR_GREEN  20
#define VINDRIKTNING_COLOR_ORANGE 60
#define VINDRIKTNING_COLOR_RED    100


#define D_PRFX_VINDRIKTNING "Vindriktning"
#define D_CMND_COLOR "Color"

const char kVindriktningCommands[] PROGMEM = D_PRFX_VINDRIKTNING "|"  // Prefix
  D_CMND_COLOR;

void (* const VindriktningCommand[])(void) PROGMEM = {
  &CmndVindriktningColor };

TasmotaSerial *VindriktningSerial;

struct VINDRIKTNING {
#ifdef VINDRIKTNING_SHOW_PM1
  uint16_t pm1_0 = 0;
#endif  // VINDRIKTNING_SHOW_PM1
  uint16_t pm2_5 = 0;
#ifdef VINDRIKTNING_SHOW_PM10
  uint16_t pm10 = 0;
#endif  // VINDRIKTNING_SHOW_PM10
  uint8_t type = 1;
  uint8_t valid = 0;
  uint8_t color = VINDRIKTNING_COLOR_GREEN;
  bool discovery_triggered = false;
} Vindriktning;

bool VindriktningReadData(void) {
  if (!VindriktningSerial->available()) {
    return false;
  }
  while ((VindriktningSerial->peek() != 0x16) && VindriktningSerial->available()) {
    VindriktningSerial->read();
  }
  if (VindriktningSerial->available() < VINDRIKTNING_DATASET_SIZE) {
    return false;
  }

  uint8_t buffer[VINDRIKTNING_DATASET_SIZE];
  uint8_t bufferTx[VINDRIKTNING_DATASET_SIZE];

  VindriktningSerial->readBytes(buffer, VINDRIKTNING_DATASET_SIZE);
  memcpy(bufferTx, buffer, VINDRIKTNING_DATASET_SIZE);
  bufferTx[6] = Vindriktning.color;
  VindriktningSerial->write(bufferTx, VINDRIKTNING_DATASET_SIZE);
  VindriktningSerial->flush();  // Make room for another burst

  AddLogBuffer(LOG_LEVEL_DEBUG_MORE, buffer, VINDRIKTNING_DATASET_SIZE);

  uint8_t crc = 0;
  for (uint32_t i = 0; i < VINDRIKTNING_DATASET_SIZE; i++) {
    crc += buffer[i];
  }
  if (crc != 0) {
    AddLog(LOG_LEVEL_DEBUG, PSTR("VDN: " D_CHECKSUM_FAILURE));
    return false;
  }

  // sample data:
  //  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
  // 16 11 0b 00 00 00 0c 00 00 03 cb 00 00 00 0c 01 00 00 00 e7
  //               |pm2_5|     |pm1_0|     |pm10 |        | CRC |
  Vindriktning.pm2_5 = (buffer[5] << 8) | buffer[6];
#ifdef VINDRIKTNING_SHOW_PM1
  Vindriktning.pm1_0 = (buffer[9] << 8) | buffer[10];
#endif  // VINDRIKTNING_SHOW_PM1
#ifdef VINDRIKTNING_SHOW_PM10
  Vindriktning.pm10 = (buffer[13] << 8) | buffer[14];
#endif  // VINDRIKTNING_SHOW_PM10

  if (!Vindriktning.discovery_triggered) {
    TasmotaGlobal.discovery_counter = 1;      // force TasDiscovery()
    Vindriktning.discovery_triggered = true;
  }
  return true;
}

/*********************************************************************************************/

void VindriktningSecond(void) {                // Every second
  if (VindriktningReadData()) {
    Vindriktning.valid = 60;
  } else {
    if (Vindriktning.valid) {
      Vindriktning.valid--;
    }
  }
}

/*********************************************************************************************/

void VindriktningInit(void) {
  Vindriktning.type = 0;
  if (PinUsed(GPIO_VINDRIKTNING_RX) && PinUsed(GPIO_VINDRIKTNING_TX)) {
    VindriktningSerial = new TasmotaSerial(Pin(GPIO_VINDRIKTNING_RX), Pin(GPIO_VINDRIKTNING_TX), 1);
    if (VindriktningSerial->begin(9600)) {
      if (VindriktningSerial->hardwareSerial()) { ClaimSerial(); }
      Vindriktning.type = 1;
    }
  }
}

#ifdef USE_WEBSERVER
const char HTTP_VINDRIKTNING_SNS[] PROGMEM =
  "{s}VINDRIKTNING " D_ENVIRONMENTAL_CONCENTRATION " %s " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}";      // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>
#endif  // USE_WEBSERVER

void VindriktningShow(bool json) {
  if (Vindriktning.valid) {
    if (json) {
      ResponseAppend_P(PSTR(",\"VINDRIKTNING\":{"));
#ifdef VINDRIKTNING_SHOW_PM1
      ResponseAppend_P(PSTR("\"PM1\":%d,"), Vindriktning.pm1_0);
#endif  // VINDRIKTNING_SHOW_PM1
      ResponseAppend_P(PSTR("\"PM2.5\":%d"), Vindriktning.pm2_5);
#ifdef VINDRIKTNING_SHOW_PM10
      ResponseAppend_P(PSTR(",\"PM10\":%d"), Vindriktning.pm10);
#endif  // VINDRIKTNING_SHOW_PM10
      ResponseJsonEnd();
#ifdef USE_DOMOTICZ
      if (0 == TasmotaGlobal.tele_period) {
#ifdef VINDRIKTNING_SHOW_PM1
        DomoticzSensor(DZ_COUNT, Vindriktning.pm1_0);	   // PM1.0
#endif  // VINDRIKTNING_SHOW_PM1
        DomoticzSensor(DZ_VOLTAGE, Vindriktning.pm2_5);	 // PM2.5
#ifdef VINDRIKTNING_SHOW_PM10
        DomoticzSensor(DZ_CURRENT, Vindriktning.pm10);	 // PM10
#endif  // VINDRIKTNING_SHOW_PM10
      }
#endif  // USE_DOMOTICZ
#ifdef USE_WEBSERVER
    } else {
#ifdef VINDRIKTNING_SHOW_PM1
        WSContentSend_PD(HTTP_VINDRIKTNING_SNS, "1", Vindriktning.pm1_0);
#endif  // VINDRIKTNING_SHOW_PM1
        WSContentSend_PD(HTTP_VINDRIKTNING_SNS, "2.5", Vindriktning.pm2_5);
#ifdef VINDRIKTNING_SHOW_PM10
        WSContentSend_PD(HTTP_VINDRIKTNING_SNS, "10", Vindriktning.pm10);
#endif  // VINDRIKTNING_SHOW_PM10
#endif  // USE_WEBSERVER
    }
  }
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

void CmndVindriktningColor(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 2)) {
    if (XdrvMailbox.payload == VINDRIKTNING_CMD_GREEN) {
      Vindriktning.color = VINDRIKTNING_COLOR_GREEN;
    } else if (XdrvMailbox.payload == VINDRIKTNING_CMD_ORANGE) {
      Vindriktning.color = VINDRIKTNING_COLOR_ORANGE;
    } else if (XdrvMailbox.payload == VINDRIKTNING_CMD_RED) {
      Vindriktning.color = VINDRIKTNING_COLOR_RED;
    }
  }
  ResponseCmndIdxNumber(Vindriktning.color); 
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns91(uint8_t function) {
  bool result = false;

  if (Vindriktning.type) {
    switch (function) {
      case FUNC_EVERY_SECOND:
        VindriktningSecond();
        break;
      case FUNC_COMMAND:
        result = DecodeCommand(kVindriktningCommands, VindriktningCommand);
        break;
      case FUNC_JSON_APPEND:
        VindriktningShow(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
	      VindriktningShow(0);
        break;
#endif  // USE_WEBSERVER
      case FUNC_INIT:
        VindriktningInit();
        break;
    }
  }
  return result;
}

#endif  // USE_VINDRIKTNING