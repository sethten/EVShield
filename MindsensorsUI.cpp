/*
 * This file is part of the Arduino SoftI2cMaster Library
 *
 * This Library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Arduino SoftI2cMaster Library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "MindsensorsUI.h"

#include <stdlib.h> /* for abs */
#include <algorithm> /* for min and max */
#include <math.h> /* for sqrt and pow */
#include "EVShieldI2C.h" /* for readInteger, and writeByte and readByte */
#include "EVShield.h" /* for SH_Bank_A define, and SH_Type_NONE and SH_COMMAND */


// Constructor, needs these parameters to initialize I2C
MindsensorsUI::MindsensorsUI(void * shield, SH_BankPort bp)
: Adafruit_ILI9341(D1,D4)
, i2c(SH_Bank_A)
, tolerance(5)
{
  i2c.init(shield, bp);
  
  Adafruit_ILI9341::begin();
  Adafruit_ILI9341::setRotation(3);
  Adafruit_ILI9341::setTextSize(2);
  Adafruit_ILI9341::setTextColor(ILI9341_WHITE, ILI9341_BLACK); // white text with a black bacground
  
  // read touchscreen calibration values from PiStorms
  
  //EVShieldBank(SH_Bank_A).sensorSetType(SH_S1, SH_Type_NONE); // call writeByte directly to save resources
  i2c.writeByte(SH_S1_MODE, SH_Type_NONE); // set BAS1 type to none so it doesn't interfere with the following i2c communicaiton
  i2c.writeByte(SH_COMMAND, SH_PS_TS_l); // copy from permanent memory to temporary memory
  
  delay(2); // normally it only takes 2 milliseconds or so to load the values
  unsigned long timeout = millis() + 1000; // wait for up to a second
  while (i2c.readByte(SH_PS_TS_CALIBRATION_DATA_READY) != 1) // wait for ready byte
  {
    delay(10); // WARNING! This is BLOCKING! Background functions (keeping WiFi connected, managing TCP/IP stack, etc.) will not run in this time and the ESP8266 may crash and reset.
    if (millis() > timeout)
    {
      break; // TODO: actual exception handling? right now the calibration just stay 0
    }
  }
  
  x1 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x00);
  y1 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x02);
  x2 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x04);
  y2 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x06);
  x3 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x08);
  y3 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x0A);
  x4 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x0C);
  y4 = i2c.readInteger(SH_PS_TS_CALIBRATION_DATA + 0x0E);
}

// Read the raw x-coordinate of the touchscreen press
uint16_t MindsensorsUI::RAW_X()
{
  return i2c.readInteger(SH_PS_TS_RAWX);
}

// Read the raw y-coordinate of the touchscreen press
uint16_t MindsensorsUI::RAW_Y()
{
  return i2c.readInteger(SH_PS_TS_RAWY);
}

// helper function to getReading
double distanceToLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) // some point and two points forming the line
{
  // multiply by 1.0 to avoid integer truncation, don't need parentheses because the multiplication operator has left-to-right associativity
  return 1.0 * abs( (y2-y1)*x0 - (x2-x1)*y0 + x2*y1 - y2*x1 ) / sqrt( pow((y2-y1),2) + pow((x2-x1),2) );
}

// (private) get raw touchscreen values, do some math using the config values, and write to the output parameters
void MindsensorsUI::getReading(uint16_t *retx, uint16_t *rety) // returnX, returnY to avoid shadowing local x, y
{
  uint16_t x = RAW_X();
  uint16_t y = RAW_Y();

  if ( x < std::min({x1,x2,x3,x4}) \
    || x > std::max({x1,x2,x3,x4}) \
    || y < std::min({y1,y2,y3,y4}) \
    || y > std::max({y1,y2,y3,y4}) )
  {
    *retx = 0;
    *rety = 0;
    return;
  }
  
  // careful not to divide by 0
  if ( y2-y1 == 0 \
    || x4-x1 == 0 \
    || y3-y4 == 0 \
    || x3-x2 == 0 )
  {
    *retx = 0;
    *rety = 0;
    return;
  }
  
  // http://math.stackexchange.com/a/104595/363240
  uint16_t dU0 = 1.0 * distanceToLine(x, y, x1, y1, x2, y2) / (y2-y1) * 320;
  uint16_t dV0 = 1.0 * distanceToLine(x, y, x1, y1, x4, y4) / (x4-x1) * 240;

  uint16_t dU1 = 1.0 * distanceToLine(x, y, x4, y4, x3, y3) / (y3-y4) * 320;
  uint16_t dV1 = 1.0 * distanceToLine(x, y, x2, y2, x3, y3) / (x3-x2) * 240;
  
  // careful not to divide by 0
  if ( dU0+dU1 == 0 \
    || dV0+dV1 == 0 )
  {
    *retx = 0;
    *rety = 0;
    return;
  }
  
  *retx = 320 * dU0/(dU0+dU1);
  *rety = 240 * dV0/(dV0+dV1);
}

// (public) read the touchscreen press and write the coordinates to the output parameters
void MindsensorsUI::getTouchscreenValues(uint16_t *x, uint16_t *y)
{
  uint16_t x1, y1;
  getReading(&x1, &y1);
  uint16_t x2, y2;
  getReading(&x2, &y2);
  
  if (abs(x2-x1) < tolerance and abs(y2-y1) < tolerance)
  {
    *x = x2;
    *y = y2;
  } else {
    *x = 0;
    *y = 0;
  }
}

// reads the x-coordinate of the touchscreen press
uint16_t MindsensorsUI::TS_X()
{
  uint16_t x, y;
  getTouchscreenValues(&x, &y);
  return x;
}

// reads the y-coordinate of the touchscreen press
uint16_t MindsensorsUI::TS_Y()
{
  uint16_t x, y;
  getTouchscreenValues(&x, &y);
  return y;
}

bool MindsensorsUI::isTouched()
{
  uint16_t x, y;
  getTouchscreenValues(&x, &y);
  return !(x==0 && y==0);
}

void MindsensorsUI::clearScreen()
{
  Adafruit_ILI9341::fillScreen(ILI9341_BLACK);
}

bool MindsensorsUI::checkButton(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
  uint16_t tsx, tsy; // touch screen x, touch screen y
  getTouchscreenValues(&tsx, &tsy);
  
  if (tsx==0 && tsy==0)
  {
    return false;
  }
  
  // 0,0 is top-left corner
  // if left of right edge, right of left edge, above bottom edge, and below top edge
  return tsx<=x+width && tsx>=x && tsy<=y+height && tsy>=y;
}

size_t MindsensorsUI::write(const uint8_t *buffer, size_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  getTextBounds((char*)buffer, getCursorX(), getCursorY(), &x1, &y1, &w, &h);
  
  if (y1+h >= height())
    setCursor(0,0);
  
  if (mirrorWriteToSerial && Serial)
    Serial.write(buffer, size);
  Print::write(buffer, size);
}

void MindsensorsUI::writeMirrorToSerial(bool enable) {
  mirrorWriteToSerial = enable; // note naming difference between variable and method
}
