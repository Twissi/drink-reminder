/*
   -------------------------------------------------------------------------------------
   Drink reminder

   Uses the following libraries:
   Arduino library for HX711 24-Bit Analog-to-Digital Converter for Weight Scales
   Arduino library for Adafruit Neopixel
   -------------------------------------------------------------------------------------
*/

/*
   Before uploading the program calibrate the weight and update the WEIGHT_CALIBRATION_VALUE
   
   Before starting the program remove the glas from the weight
   After the led shows red, put on the glas filled with water
   Each day you need to restart the program in order to start with 0 total water amount

   Color coding:
   Blinking red led: Starting up
   Red led: glas is missing
   Blinking purple led: measurement in progress
   Blinking blue leds: Drink reminder!
   Green led: Drink progress towards your goal
*/

#ifdef __AVR__
#include <avr/power.h>
#endif

#include <HX711_ADC.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

// pins:
const int HX711_dout = 4; //mcu > HX711 dout pin
const int HX711_sck = 5; //mcu > HX711 sck pin
const int LEDSTRIP_pin = 11; // led strip data pin

// configuration
const int LEDS_COUNT = 15;
const long REMINDER_INTERVAL = 600000; //10 min reminder interval
const int WATERMAX_ML = 2000; //2000 ml drink target
const float WEIGHT_CALIBRATION_VALUE = 385.46; //calibration value (see example file "Calibration.ino" of HX711 lib)

// logs:
const bool LOG_WEIGHT = 0;
const bool LOG_UNSTABLE_MEASUREMENT = 0;
const bool LOG_TOTAL_AMOUNT_PIXEL = 0;
const bool LOG_LAST_WEIGHT = 0;

// HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// NeoPixel constructor:
Adafruit_NeoPixel strip = Adafruit_NeoPixel(LEDS_COUNT, LEDSTRIP_pin, NEO_GRB + NEO_KHZ800);


// Global variables
long prevTime;
float totalWaterAmount = 0;
bool isLoaded = 0;
bool prevIsLoaded = 0;
float lastWeightLoaded;
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
  displayBlinkingColor(strip.Color(255, 0, 0), 50);

  LoadCell.begin();
  float calibrationValue = WEIGHT_CALIBRATION_VALUE;
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
    displayColor(strip.Color(255, 0, 0), 0);
  }
}

void loop() {
  static boolean newDataReady = 0;
  const int serialPrintInterval = 0; // increase value to slow down serial print activity

  // check for new data/start next conversion:
  if (LoadCell.update()) newDataReady = true;

  // get smoothed value from the dataset
  if (newDataReady) {
    if (millis() > prevTime + serialPrintInterval) {
      float i = LoadCell.getData();
      // action to be done after new weight was measured
      onNewWeight(i);
      newDataReady = 0;
      prevTime = millis();
    }
  }

  // drink reminder
  if (millis() >= lastReminderMillis + REMINDER_INTERVAL) {
    Serial.println("Don't forget to drink water!");
    lastReminderMillis = millis();
    saveLedColor(lastLedColor);
    displayBlinkingColor(strip.Color(0, 0, 127), 50);
    resetLedColor();
  }
}

// On weight cell load - new weight found
void onNewWeight(float weight) {
  if (LOG_WEIGHT) {
    Serial.print("Current weight: ");
    Serial.println(weight);
  }

  if (!isStableValue(weight)) {
    return;
  }

  if (weight < 100.00) {
    isLoaded = 0;
  } else {
    isLoaded = 1;
  }


  // detect state change (glas was put on)
  if (isLoaded != prevIsLoaded) {
    prevIsLoaded = isLoaded;

    if (isLoaded) {
      if (LOG_LAST_WEIGHT) {
        Serial.print("Last onload weight: ");
        Serial.println(lastWeightLoaded);
      }
      
      if (!lastWeightLoaded) {
        lastWeightLoaded = weight;
        displayColor(strip.Color(0, 255, 0), 50); // Green led = glas found
        displayColor(strip.Color(0, 0, 0, 255), 0); // White
        return;
      }
      // check if water decreased
      if (lastWeightLoaded > weight + 1.00) {
        totalWaterAmount = totalWaterAmount + lastWeightLoaded - weight;
        // reset reminder
        lastReminderMillis = millis();
        Serial.print("Good job, water decreased! Your drank in total:");
        Serial.println(totalWaterAmount);
      }

      lastWeightLoaded = weight;  
      
      displayTotalWaterAmount(strip.Color(0, 255, 0), 0); // Green led = glas found, show total amount
      displayColor(strip.Color(0, 0, 0, 255), 0);
    } else {
      displayColor(strip.Color(255, 0, 0), 0); // Red led = glas missing
    }
  }
}

// return if weight is stable or not
bool isStableValue(float weight) {
  if (abs(lastWeightMeasurement) + 0.20 > abs(weight) && abs(lastWeightMeasurement) - 0.20 < abs(weight)) {
    lastWeightMeasurement = weight;
    return true;
  }

  if (LOG_UNSTABLE_MEASUREMENT) {
    Serial.println("Unstable measurement, please wait!");
  }
  displayColor(strip.Color(255, 0, 213), 10);
  displayColor(strip.Color(0, 0, 0, 255), 0); // White
  lastWeightMeasurement = weight;
  return false;
}

// save led color
void saveLedColor(uint32_t c) {
  savedLedColor = c;
}

// set led strip to saved led color
void resetLedColor() {
  displayColor(savedLedColor, 0);
}

// LEDStrip animation: Fill the dots one after the other with a color
void displayColor(uint32_t c, uint8_t wait) {
  lastLedColor = c;
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

// LEDStrip animation: Only fill as many dots mapping the totalWaterAmount in percent
void displayTotalWaterAmount(uint32_t c, uint8_t wait) {
  lastLedColor = c;
  int amountPerPixel = floor(WATERMAX_ML / strip.numPixels());
  int numberOfPixels = ceil(totalWaterAmount / amountPerPixel);

  if (numberOfPixels > strip.numPixels()) {
    numberOfPixels = strip.numPixels();
  }
  if (LOG_TOTAL_AMOUNT_PIXEL) {
    Serial.print("Show total amount pixel(s): ");
    Serial.println(numberOfPixels);
  }
  lastLedColor = c;
  for (uint16_t i = 0; i < numberOfPixels; i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

// LEDStrip animation: Theatre-style crawling lights.
void displayBlinkingColor(uint32_t c, uint8_t wait) {
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
