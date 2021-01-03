/*
   -------------------------------------------------------------------------------------
   Drink reminder

   Uses:
   Arduino library for HX711 24-Bit Analog-to-Digital Converter for Weight Scales
   Arduino library for Adafruit Neopixel
   -------------------------------------------------------------------------------------
*/

/*
   Settling time (number of samples) and data filtering can be adjusted in the config.h file
   For calibration and storing the calibration value in eeprom, see example file "Calibration.ino"

   The update() function checks for new data and starts the next conversion. In order to acheive maximum effective
   sample rate, update() should be called at least as often as the HX711 sample rate; >10Hz@10SPS, >80Hz@80SPS.
   If you have other time consuming code running (i.e. a graphical LCD), consider calling update() from an interrupt routine,
   see example file "Read_1x_load_cell_interrupt_driven.ino".

*/

#ifdef __AVR__
#include <avr/power.h>
#endif

#include <HX711_ADC.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

//pins:
const int HX711_dout = 4; //mcu > HX711 dout pin
const int HX711_sck = 5; //mcu > HX711 sck pin
const int LEDSTRIP = 13; // led strip pin

// configuration
const long REMINDER_INTERVAL = 600000; //10min
const int WATERMAX_ML = 2000; //in ml

//logs:
const bool LOG_WEIGHT = 1;
const bool LOG_UNSTABLE_MEASUREMENT = 0;

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// NeoPixel constructor:
Adafruit_NeoPixel strip = Adafruit_NeoPixel(15, LEDSTRIP, NEO_GRB + NEO_KHZ800);

const int calVal_eepromAdress = 0;
long t;
float sumWaterAmount = 0;
bool glasFound = 0;
bool prevGlasFound = 0;
float lastWaterAmount = 0;
float lastWeightMeasurement = 0;
long lastReminderMillis = 0;
uint32_t lastLedColor = strip.Color(0, 0, 0, 255);
uint32_t savedLedColor = strip.Color(0, 0, 0, 255);

void setup() {
  Serial.begin(57600); delay(10);
  Serial.println();
  Serial.println("Starting...");

  // strip setup
  strip.begin();
  theaterChase(strip.Color(255, 0, 0), 50);

  LoadCell.begin();
  float calibrationValue; // calibration value (see example file "Calibration.ino")
  calibrationValue = 385.46; // uncomment this if you want to set the calibration value in the sketch
#if defined(ESP8266)|| defined(ESP32)
  //EEPROM.begin(512); // uncomment this if you use ESP8266/ESP32 and want to fetch the calibration value from eeprom
#endif
  //EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch the calibration value from eeprom

  long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    Serial.println("Startup is complete");
  }
}

void loop() {
  static boolean newDataReady = 0;
  const int serialPrintInterval = 0; //increase value to slow down serial print activity

  // check for new data/start next conversion:
  if (LoadCell.update()) newDataReady = true;

  // get smoothed value from the dataset:
  if (newDataReady) {
    if (millis() > t + serialPrintInterval) {
      float i = LoadCell.getData();
      onCellLoad(i);
      newDataReady = 0;
      t = millis();
    }
  }

  // Drink reminder
  if (millis() >= lastReminderMillis + REMINDER_INTERVAL) {
    Serial.println("Don't forget to drink water");
    lastReminderMillis = millis();
    saveLedColor(lastLedColor);
    theaterChase(strip.Color(0, 0, 127), 50);
    resetLedColor();
  }

  // receive command from serial terminal, send 't' to initiate tare operation:
  if (Serial.available() > 0) {
    float i;
    char inByte = Serial.read();
    if (inByte == 't') LoadCell.tareNoDelay();
  }

  // check if last tare operation is complete:
  if (LoadCell.getTareStatus() == true) {
    Serial.println("Tare complete");
  }

}

// on cell load
void onCellLoad(float weight) {

  if (LOG_WEIGHT) {
    Serial.print("Current weight: ");
    Serial.println(weight);
  }

  if (!isStableValue(weight)) {
    return;
  }

  if (weight < 1.00) {
    glasFound = 0;
  } else {
    glasFound = 1;
  }


  // detect state change (glas was put back)
  if (glasFound != prevGlasFound) {
    prevGlasFound = glasFound;

    if (glasFound) {
      Serial.print("Last known weight: ");
      Serial.println(lastWaterAmount);
      // just started
      if (lastWaterAmount == 0.00) {
        lastWaterAmount = weight;
        colorWipe(strip.Color(0, 255, 0), 50); // Green led = glas found
        colorWipe(strip.Color(0, 0, 0, 255), 0); // White
        return;
      }
      // water increased
      if (lastWaterAmount > weight + 1.00) {
        sumWaterAmount = sumWaterAmount + lastWaterAmount - weight;
        // reset reminder
        lastReminderMillis = millis();
        Serial.print("Good job, water decreased! Your total:");
        Serial.println(sumWaterAmount);
      }
      lastWaterAmount = weight;

      colorWipePercent(strip.Color(0, 255, 0), 50); // Green led = glas found
      colorWipe(strip.Color(0, 0, 0, 255), 0);
    } else {
      colorWipe(strip.Color(255, 0, 0), 0); // Red led = glas missing
    }
  }
}

bool isStableValue(float weight) {
  // todo: at the beginning could be 0, lastWeightMeasurement == 0

  if (abs(lastWeightMeasurement) + 0.20 > abs(weight) && abs(lastWeightMeasurement) - 0.20 < abs(weight)) {
    lastWeightMeasurement = weight;
    return true;
  }

  if (LOG_UNSTABLE_MEASUREMENT) {
    Serial.println("Unstable measurement, please wait!");
  }
  colorWipe(strip.Color(255, 0, 213), 10);
  colorWipe(strip.Color(0, 0, 0, 255), 0); // White
  lastWeightMeasurement = weight;
  return false;
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  lastLedColor = c;
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

void colorWipePercent(uint32_t c, uint8_t wait) {
  int amountPerPixel = floor(WATERMAX_ML / strip.numPixels());
  int numberOfPixels = floor(sumWaterAmount / amountPerPixel);

  if (numberOfPixels > strip.numPixels()) {
    numberOfPixels = strip.numPixels();
  }
  Serial.print("Highlight pixel: ");
  Serial.println(numberOfPixels);
  lastLedColor = c;
  for (uint16_t i = 0; i < numberOfPixels; i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

void saveLedColor(uint32_t c) {
  savedLedColor = c;
}

void resetLedColor() {
  colorWipe(savedLedColor, 0);
}

//Theatre-style crawling lights.
void theaterChase(uint32_t c, uint8_t wait) {
  for (int j = 0; j < 10; j++) { //do 10 cycles of chasing
    for (int q = 0; q < 3; q++) {
      for (uint16_t i = 0; i < strip.numPixels(); i = i + 3) {
        strip.setPixelColor(i + q, c);  //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (uint16_t i = 0; i < strip.numPixels(); i = i + 3) {
        strip.setPixelColor(i + q, 0);      //turn every third pixel off
      }
    }
  }
}
