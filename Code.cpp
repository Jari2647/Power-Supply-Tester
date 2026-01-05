#include <LiquidCrystal.h>

// PIN DEFINITIONS
// LCD 16x2 (WC1602A) in 4-bit mode
const int LCD_RS = 53;
const int LCD_E  = 51;
const int LCD_D4 = 49;
const int LCD_D5 = 47;
const int LCD_D6 = 45;
const int LCD_D7 = 43;

// MOSFET gate pins (low-side switches for load resistors)
const int PIN_FET_3V3 = 8;   // FET3 -> Q1 gate
const int PIN_FET_5V  = 9;   // FET2 -> Q2 gate
const int PIN_FET_12V = 10;   // FET1 -> Q3 gate

// Buttons: one for menu, one for load toggle, third unused/spare
const int PIN_BTN_MENU = 25;  // SW2 via R18
const int PIN_BTN_LOAD = 27;  // SW3 via R19
const int PIN_BTN_SPARE = 29; // SW4 via R13 (currently unused)

// Analog inputs from your dividers
const int PIN_MB12V = A0;     // R12=6.73k, R13=3.238k
const int PIN_MB5V  = A1;     // R14=9.89k, R15=10k
const int PIN_MB3V3 = A2;     // R16=10.02k, R17=9.86k

// CALIBRATION
const float ADC_REF_V = 5.0;    // measured 5 V reference

// Measured divider resistor values (ohms)
const float MB12_R_TOP    = 6730.0;   // R12
const float MB12_R_BOTTOM = 3238.0;   // R13

const float MB5_R_TOP     = 9890.0;  // R14
const float MB5_R_BOTTOM  = 10000.0;  // R15

const float MB3_R_TOP     = 10020.0;  // R16
const float MB3_R_BOTTOM  = 9860.0;  // R117

// ATX spec ranges (±5 %)
const float MB12_MIN = 11.40;
const float MB12_MAX = 12.60;
const float MB5_MIN  = 4.75;
const float MB5_MAX  = 5.25;
const float MB3_MIN  = 3.135;
const float MB3_MAX  = 3.465;

// PSU considered "on" if 12 V rail above this (V)
const float PSU_ON_THRESHOLD = 1.0;

// GLOBAL STATE
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

bool psuOn  = false;   // derived from 12 V rail
bool loadOn = false;   // whether MOSFETs are enabled
int  currentPage = 0;  // 0 = voltages, 1 = pass/fail

// Edge-detection helpers
bool lastMenuState  = false;
bool lastLoadState  = false;
bool lastSpareState = false;

// UTILS
bool buttonPressed(int pin, bool &lastState) {
  bool now = (digitalRead(pin) == HIGH);  // HIGH when pressed
  bool pressed = (now && !lastState);
  lastState = now;
  return pressed;
}

float readAnalogAveraged(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
  }
  float avg = sum / (float)samples;
  return (avg * ADC_REF_V) / 1023.0;   // voltage at Arduino pin
}

float dividerToRail(float vadc, float rTop, float rBottom) {
  return vadc * (rTop + rBottom) / rBottom;
}

bool inRange(float v, float vmin, float vmax) {
  return (v >= vmin && v <= vmax);
}

void setLoad(bool on) {
  loadOn = on;
  digitalWrite(PIN_FET_3V3, on ? HIGH : LOW);
  digitalWrite(PIN_FET_5V,  on ? HIGH : LOW);
  digitalWrite(PIN_FET_12V, on ? HIGH : LOW);
}

// DISPLAY
void showVoltages(float v12, float v5, float v3) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("12:");
  lcd.print(v12, 2);
  lcd.print(" 5:");
  lcd.print(v5, 2);

  lcd.setCursor(0, 1);
  lcd.print("3:");
  lcd.print(v3, 2);
  lcd.print(" ");
  lcd.print(psuOn ? "P:Y" : "P:N");
  lcd.print(" ");
  lcd.print(loadOn ? "L:Y" : "L:N");
}

void showStatus(float v12, float v5, float v3) {
  bool ok12 = inRange(v12, MB12_MIN, MB12_MAX);
  bool ok5  = inRange(v5,  MB5_MIN,  MB5_MAX);
  bool ok3  = inRange(v3,  MB3_MIN,  MB3_MAX);

  lcd.clear();
  lcd.setCursor(0, 0);
  if (ok12 && ok5 && ok3) {
    lcd.print("ATX: PASS");
  } else {
    lcd.print("ATX: FAIL");
  }

  lcd.setCursor(0, 1);
  if (!ok12) lcd.print("12 ");
  if (!ok5)  lcd.print("5 ");
  if (!ok3)  lcd.print("3.3 ");
  if (ok12 && ok5 && ok3) lcd.print("all ok");
}

// SETUP & LOOP
void setup() {
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ATX PSU Tester");
  lcd.setCursor(0, 1);
  lcd.print("Flip PSU switch");

  pinMode(PIN_FET_3V3, OUTPUT);
  pinMode(PIN_FET_5V,  OUTPUT);
  pinMode(PIN_FET_12V, OUTPUT);

  pinMode(PIN_BTN_MENU, INPUT);
  pinMode(PIN_BTN_LOAD, INPUT);
  pinMode(PIN_BTN_SPARE, INPUT);

  pinMode(PIN_MB12V, INPUT);
  pinMode(PIN_MB5V,  INPUT);
  pinMode(PIN_MB3V3, INPUT);

  setLoad(false);
}

void loop() {
  // Read rails
  float v12_adc = readAnalogAveraged(PIN_MB12V);
  float v5_adc  = readAnalogAveraged(PIN_MB5V);
  float v3_adc  = readAnalogAveraged(PIN_MB3V3);

  float v12 = dividerToRail(v12_adc, MB12_R_TOP, MB12_R_BOTTOM);
  float v5  = dividerToRail(v5_adc,  MB5_R_TOP,  MB5_R_BOTTOM);
  float v3  = dividerToRail(v3_adc,  MB3_R_TOP,  MB3_R_BOTTOM);

  float v12_cal = v12 * 1.0025;
  float v5_cal  = v5  * 1.0163;
  float v3_cal  = v3  * 0.9906;

  // Derive PSU on/off from 12 V rail
  psuOn = (v12 > PSU_ON_THRESHOLD);

  // If PSU is off, force loads off
  if (!psuOn && loadOn) {
    setLoad(false);
  }

  // Handle buttons
  if (buttonPressed(PIN_BTN_MENU, lastMenuState)) {
    currentPage = (currentPage + 1) % 2; // toggle pages
  }

  if (buttonPressed(PIN_BTN_LOAD, lastLoadState)) {
    if (psuOn) {
      setLoad(!loadOn);
    } else {
      setLoad(false);
    }
  }

  // Spare button currently unused
  (void)buttonPressed(PIN_BTN_SPARE, lastSpareState);

  // Update display
  if (currentPage == 0) {
    showVoltages(v12_cal, v5_cal, v3_cal);
  } else {
    showStatus(v12_cal, v5_cal, v3_cal);
  }

  delay(250);
}
