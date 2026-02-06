// -----------------------------------------------------------------------------
// Types (placés avant le préprocesseur Arduino) pour éviter les erreurs de prototypes
// -----------------------------------------------------------------------------
enum Mode { MODE_MEDIT = 0, MODE_REIKI = 1, MODE_SPORT = 2, MODE_FOCUS = 3 };

enum SportPhase { SPORT_WORK = 0, SPORT_REST = 1 };
enum FocusPhase { FOCUS_WORK = 0, FOCUS_BREAK = 1 };

enum AppState {
  STATE_MODE_SELECT = 0,
  STATE_REIKI_CONFIG,
  STATE_MEDIT_CONFIG,
  STATE_SPORT_CONFIG,
  STATE_FOCUS_CONFIG,
  STATE_TIME_EDIT,
  STATE_REIKI_RUNNING,
  STATE_MEDIT_RUNNING,
  STATE_SPORT_RUNNING,
  STATE_FOCUS_RUNNING,
  STATE_DONE
};

enum TimeEditTarget {
  TE_NONE = 0,
  TE_REIKI_INTERVAL,
  TE_MEDIT_TOTAL,
  TE_SPORT_WORK,
  TE_SPORT_REST,
  TE_FOCUS_WORK,
  TE_FOCUS_BREAK
};

#include <Arduino.h>
#include <math.h>

// Bibliothèques E-Paper
#include <GxEPD.h>
#include <GxDEPG0150BN/GxDEPG0150BN.h>    // 1.54" b/w 200x200
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

// Gestion Bouton
#include <AceButton.h>
using namespace ace_button;

// -----------------------------------------------------------------------------
// Bitmaps (images.h)
// Si ton ancien nom existe encore, tu peux décommenter les alias suivants :
//   #define sport_bitmap sport_bitmap_musculation
//   #define focus_bitmap bitmap_meditation
// -----------------------------------------------------------------------------
#include "images.h"

static void drawActionButton(const char* label, int16_t yTop, bool selected);
static void epdDrawScrubPattern();

// --- SPORT ICON (dans images.h) ---
// Par défaut on attend: sport_bitmap (48x48).
// Si ton bitmap a une autre taille, ajuste SPORT_ICON_W/H.
#ifndef SPORT_BITMAP
  #define SPORT_BITMAP sport_bitmap
#endif
#ifndef SPORT_ICON_W
  #define SPORT_ICON_W 48
#endif
#ifndef SPORT_ICON_H
  #define SPORT_ICON_H 48
#endif



// --- FOCUS ICON (dans images.h) ---
// Ajoute un bitmap focus_bitmap (48x48) dans images.h.
#ifndef FOCUS_BITMAP
  #define FOCUS_BITMAP focus_bitmap
#endif
#ifndef FOCUS_ICON_W
  #define FOCUS_ICON_W 48
#endif
#ifndef FOCUS_ICON_H
  #define FOCUS_ICON_H 48
#endif

// -----------------------------------------------------------------------------
// PINS & CONSTANTES HARDWARE
// -----------------------------------------------------------------------------
#define PIN_MOTOR     4
#define PIN_KEY       35
#define PWR_EN        5   
#define BACKLIGHT     33  
#define BAT_ADC       34  

#define SPI_SCK       14
#define SPI_DIN       13
#define EPD_CS        15
#define EPD_DC        2
#define EPD_RESET     17
#define EPD_BUSY      16

#define MOTOR_PWM_CH  1
#define PWM_FREQ      1000
#define PWM_RES       8 

GxIO_Class io(SPI, EPD_CS, EPD_DC, EPD_RESET);
GxEPD_Class display(io, EPD_RESET, EPD_BUSY);

AceButton button(PIN_KEY);

const int16_t SCREEN_W = 200;
const int16_t SCREEN_H = 200;

// -----------------------------------------------------------------------------
// CONFIGURATION AVEC MÉMOIRE RTC (Survit au Deep Sleep)
// -----------------------------------------------------------------------------


RTC_DATA_ATTR Mode currentMode = MODE_MEDIT;       

// --- REIKI ---
RTC_DATA_ATTR uint8_t  reikiNumIntervals = 1;
RTC_DATA_ATTR uint16_t reikiIntervalSec  = 3 * 60;   // durée d'une position (en secondes)

// --- MEDITATION ---
RTC_DATA_ATTR uint16_t meditTotalSec = 10 * 60;      // durée totale (en secondes)
RTC_DATA_ATTR bool meditationMidAlert = true;

// --- SPORT (Interval timer) ---
RTC_DATA_ATTR uint16_t sportWorkSec = 45;   // durée effort
RTC_DATA_ATTR uint16_t sportRestSec = 15;   // durée repos
RTC_DATA_ATTR uint8_t  sportRounds  = 10;   // nombre de rounds

// --- FOCUS (Pomodoro) ---
RTC_DATA_ATTR uint16_t focusWorkSec  = 25 * 60; // durée travail (secondes)
RTC_DATA_ATTR uint16_t focusBreakSec =  5 * 60; // durée pause (secondes)
RTC_DATA_ATTR uint8_t  focusSets     = 4;       // nombre de cycles (work)




// -----------------------------------------------------------------------------
// VARIABLES D'ÉTAT
// -----------------------------------------------------------------------------

AppState appState = STATE_MODE_SELECT;
Mode modeSelectHighlight = MODE_MEDIT; 
Mode lastMode = MODE_MEDIT;          

volatile bool uiNeedsUpdate = false;      
volatile bool uiFullRefresh = false;      
volatile unsigned long lastInteractionTime = 0; 
const unsigned long SLEEP_TIMEOUT_MS = 60000; 

uint8_t reikiMenuIndex = 0;
uint8_t meditMenuIndex = 0;
uint8_t sportMenuIndex = 0;

uint8_t focusMenuIndex = 0;
// Runtime
bool reikiRunning = false;
unsigned long reikiSessionStartMs = 0;
uint32_t reikiLastDrawSec = 0;

bool meditRunning = false;
bool meditMidAlertDone = false;
unsigned long meditSessionStartMs = 0;
uint32_t meditLastDrawSec = 0;

bool sportRunning = false;
unsigned long sportSessionStartMs = 0;
uint32_t sportLastDrawSec = 0;
uint8_t sportLastPhase = 255; // 0=WORK,1=REST
uint8_t sportLastRound = 0;

// Focus runtime
bool focusRunning = false;
unsigned long focusSessionStartMs = 0;
uint32_t focusLastDrawSec = 0;
uint8_t focusLastPhase = 255; // 0=WORK,1=BREAK
uint8_t focusLastSet = 0;

// -----------------------------------------------------------------------------
// PROTOTYPES
// -----------------------------------------------------------------------------
void goToDeepSleep();
void refreshScreen(); 
void stopSession(bool finished);
void requestVibe(uint8_t strength, uint16_t ms);
void serviceVibe(); 
void triggerVibePattern(int pattern);
float readBatteryVoltage();

inline void requestUi(bool full = false) {
  uiNeedsUpdate = true;
  if (full) uiFullRefresh = true;
}

// -----------------------------------------------------------------------------
// VIBRATION MANAGER
// -----------------------------------------------------------------------------
volatile uint8_t vibStrength = 0;
volatile uint32_t vibUntilMs = 0;

void requestVibe(uint8_t strength, uint16_t ms) {
  vibStrength = strength;
  vibUntilMs = millis() + ms;
  ledcWrite(MOTOR_PWM_CH, 0); 
  delayMicroseconds(100); 
  ledcWrite(MOTOR_PWM_CH, vibStrength);
}

void serviceVibe() {
  if (vibStrength != 0 && (long)(millis() - vibUntilMs) >= 0) {
    ledcWrite(MOTOR_PWM_CH, 0);
    vibStrength = 0;
  }
}

void triggerVibePattern(int pattern) {
  if (pattern == 1) { // Click
     requestVibe(120, 30); 
  } else if (pattern == 2) { // Confirmation
     requestVibe(150, 50); delay(80); requestVibe(150, 50);
  } else if (pattern == 3) { // Changement intervalle doux
     for(int i=0; i<3; i++) { requestVibe(80 + (i*20), 200); delay(300); }
  } else if (pattern == 4) { // Fin session
     requestVibe(200, 500); delay(600); requestVibe(200, 500);
  } else if (pattern == 5) { // Alerte Mi-parcours (3 coups distincts)
     for(int i=0; i<3; i++) { requestVibe(200, 200); delay(350); }
  }
}

// -----------------------------------------------------------------------------
// TACHE BOUTON (Core 0)
// -----------------------------------------------------------------------------
void buttonTask(void * parameter) {
  while(true) {
    button.check(); 
    serviceVibe(); 
    vTaskDelay(pdMS_TO_TICKS(5)); 
  }
}

// -----------------------------------------------------------------------------
// UTILITAIRES & BATTERIE
// -----------------------------------------------------------------------------
void resetSleepTimer() { lastInteractionTime = millis(); }

uint8_t batteryPercentFromVoltage(float v) {
  if (v <= 3.30f) return 0;
  if (v >= 4.15f) return 100; 
  int pct = (int)((v - 3.30f) * 100.0f / (4.15f - 3.30f));
  return constrain(pct, 0, 100);
}

String formatTimeMMSS(uint32_t seconds) {
  uint32_t m = seconds / 60;
  uint32_t s = seconds % 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", m, s);
  return String(buf);
}

static const float VBAT_DIVIDER = 2.0f; 
float readBatteryVoltage() {
  uint32_t sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogReadMilliVolts(BAT_ADC); 
    delay(1);
  }
  return (sum / 10.0f / 1000.0f) * VBAT_DIVIDER;
}

// -----------------------------------------------------------------------------
// AFFICHAGE
// -----------------------------------------------------------------------------
void printCentered(const String &text, int16_t y, const GFXfont* font = NULL) {
  int16_t x1, y1;
  uint16_t w, h;
  if(font) display.setFont(font);
  else display.setFont(); 
  
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_W - w) / 2;
  display.setCursor(x, y);
  display.print(text);
  display.setFont(); 
}

void drawBatteryIcon() {
  float v = readBatteryVoltage();
  uint8_t pct = batteryPercentFromVoltage(v);
  display.drawRect(170, 5, 24, 10, GxEPD_BLACK);
  display.fillRect(194, 7, 2, 6, GxEPD_BLACK);
  uint8_t w = map(pct, 0, 100, 0, 20);
  display.fillRect(172, 7, w, 6, GxEPD_BLACK);
}

uint16_t getReikiIntervalSec() { return reikiIntervalSec; }
uint16_t getMeditTotalSec() { return meditTotalSec; }
// --- ECRANS ---

void drawModeSelectScreen() {
  display.fillScreen(GxEPD_WHITE);
  drawBatteryIcon();

  // 4 modes, grille 2x2 : 2 à gauche, 2 à droite
  const int16_t iconW = 48;
  const int16_t iconH = 48;

  const int16_t xCenters[2] = {55, 145};
  const int16_t yCenters[2] = {60, 140};

  // Ordre de navigation (click)
  Mode modes[4] = { MODE_MEDIT, MODE_REIKI, MODE_SPORT, MODE_FOCUS };

  for (uint8_t idx = 0; idx < 4; idx++) {
    uint8_t col = idx % 2;
    uint8_t row = idx / 2;

    int16_t cx = xCenters[col];
    int16_t cy = yCenters[row];

    int16_t x = cx - iconW / 2;
    int16_t y = cy - iconH / 2;

    bool hl = (modeSelectHighlight == modes[idx]);
    if (hl) {
      display.fillCircle(cx, cy, 34, GxEPD_BLACK);
    }

    if (modes[idx] == MODE_MEDIT) {
      display.drawBitmap(x, y, bitmap_meditation, 48, 48, hl ? GxEPD_WHITE : GxEPD_BLACK);
    } else if (modes[idx] == MODE_REIKI) {
      display.drawBitmap(x, y, bitmap_reiki, 48, 48, hl ? GxEPD_WHITE : GxEPD_BLACK);
    } else if (modes[idx] == MODE_SPORT) {
      display.drawBitmap(x, y, SPORT_BITMAP, SPORT_ICON_W, SPORT_ICON_H, hl ? GxEPD_WHITE : GxEPD_BLACK);
    } else { // MODE_FOCUS
      display.drawBitmap(x, y, FOCUS_BITMAP, FOCUS_ICON_W, FOCUS_ICON_H, hl ? GxEPD_WHITE : GxEPD_BLACK);
    }
  }

  display.setTextColor(GxEPD_BLACK);
  printCentered("Click: Next | Hold: OK", 190);
}



void drawGenericConfig(const String& title, int selectedIndex, int numItems, String items[]) {
  display.fillScreen(GxEPD_WHITE);
  drawBatteryIcon();

  display.setFont(&FreeSansBold9pt7b);
  printCentered(title, 25, &FreeSansBold9pt7b);
  display.drawLine(20, 30, 180, 30, GxEPD_BLACK);

  int16_t baseY = 60;
  int16_t lineH = 35;

  display.setFont(); 
  display.setTextSize(2);

  for (uint8_t i = 0; i < numItems; i++) {
    int16_t y = baseY + i * lineH;

    // Special rendering for START: centered + bold button style (same as "Valider")
    if (items[i] == "START") {
      drawActionButton("START", y - 10, (selectedIndex == i));
      continue;
    }

    if (selectedIndex == i) {
      display.fillRect(0, y - 5, SCREEN_W, lineH, GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
    } else {
      display.setTextColor(GxEPD_BLACK);
    }
    display.setCursor(15, y + 5);
    display.print(items[i]);
  }

  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  printCentered("Click: Next | Hold: Edit/Go", 190);
}


// -----------------------------------------------------------------------------
// TIME EDITOR
// Click  = change value (digit)
// Hold   = next field
// Valider = save & return
//
// Notes:
// - If minutesOnly == true => Minutes-only editor (seconds forced to 00 and hidden)
// - Else                   => MM:SS editor (minutes + seconds)
// -----------------------------------------------------------------------------
TimeEditTarget timeEditTarget = TE_NONE;
AppState timeEditReturnState = STATE_MODE_SELECT;

uint16_t timeEditSec = 0;
uint16_t timeEditMinSec = 0;
uint16_t timeEditMaxSec = 5999;
bool timeEditMinutesOnly = false;

static const uint8_t TE_FIELD_VALIDATE = 255;

// Field order: digit indices (0..digitCount-1) then TE_FIELD_VALIDATE
uint8_t timeEditFieldOrder[6];
uint8_t timeEditFieldCount = 0;
uint8_t timeEditFieldIndex = 0;
uint8_t timeEditDigitCount = 4;

char timeEditTitle[20] = "TIMER";

static void rebuildFieldOrder() {
  if (timeEditMinutesOnly) {
    uint16_t maxMin = timeEditMaxSec / 60;
    timeEditDigitCount = (maxMin >= 100) ? 3 : 2;  // 0..999 minutes
  } else {
    timeEditDigitCount = 4; // MM:SS => 4 digits
  }

  for (uint8_t i = 0; i < timeEditDigitCount; i++) timeEditFieldOrder[i] = i;
  timeEditFieldOrder[timeEditDigitCount] = TE_FIELD_VALIDATE;

  timeEditFieldCount = timeEditDigitCount + 1;
  timeEditFieldIndex = 0;
}

static void clampOrWrapTimeEdit() {
  if (timeEditSec < timeEditMinSec) timeEditSec = timeEditMinSec;
  if (timeEditSec > timeEditMaxSec) timeEditSec = timeEditMinSec; // wrap
}

static void applyTimeEditValue() {
  // If minutes-only, always clamp seconds to 00
  if (timeEditMinutesOnly) {
    timeEditSec = (timeEditSec / 60) * 60;
  }
  clampOrWrapTimeEdit();

  switch (timeEditTarget) {
    case TE_REIKI_INTERVAL: reikiIntervalSec = timeEditSec; break;
    case TE_MEDIT_TOTAL:    meditTotalSec    = timeEditSec; break;
    case TE_SPORT_WORK:     sportWorkSec     = timeEditSec; break;
    case TE_SPORT_REST:     sportRestSec     = timeEditSec; break;
    case TE_FOCUS_WORK:     focusWorkSec     = timeEditSec; break;
    case TE_FOCUS_BREAK:    focusBreakSec    = timeEditSec; break;
    default: break;
  }
}

static void enterTimeEdit(TimeEditTarget target, const char* title, uint16_t initialSec,
                          uint16_t minSec, uint16_t maxSec, bool minutesOnly,
                          AppState returnState) {
  timeEditTarget = target;
  timeEditReturnState = returnState;
  timeEditSec = initialSec;
  timeEditMinSec = minSec;
  timeEditMaxSec = maxSec;
  timeEditMinutesOnly = minutesOnly;

  strncpy(timeEditTitle, title, sizeof(timeEditTitle) - 1);
  timeEditTitle[sizeof(timeEditTitle) - 1] = '\0';

  if (timeEditMinutesOnly) timeEditSec = (timeEditSec / 60) * 60;
  clampOrWrapTimeEdit();
  rebuildFieldOrder();

  appState = STATE_TIME_EDIT;
  requestUi(true);
}

// Centered printing around an arbitrary X (useful for labels above fields)
static void printCenteredAtX(const String& text, int16_t centerX, int16_t y, const GFXfont* font = NULL) {
  int16_t x1, y1;
  uint16_t w, h;
  if (font) display.setFont(font); else display.setFont();
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = centerX - (int16_t)(w / 2);
  display.setCursor(x, y);
  display.print(text);
  display.setFont();
}

// Bold effect for the built-in font (by drawing twice)
static void printCenteredBoldDefault(const String& text, int16_t yTop) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_W - w) / 2;
  display.setCursor(x, yTop);
  display.print(text);
  display.setCursor(x + 1, yTop);
  display.print(text);
}

// Shared button style (START / Valider)
static void drawActionButton(const char* label, int16_t yTop, bool selected) {
  // Nicer rounded button for START / VALIDER
  const int16_t bw = 130, bh = 36;
  const int16_t bx = (SCREEN_W - bw) / 2;
  const int16_t by = yTop;
  const int16_t r  = 10; // corner radius

  if (selected) {
    // Filled black with a thin white inner stroke (reads well, looks "button-like")
    display.fillRoundRect(bx, by, bw, bh, r, GxEPD_BLACK);
    if (r > 1) display.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, r - 1, GxEPD_WHITE);
    display.setTextColor(GxEPD_WHITE);
  } else {
    // White fill + double outline for a thicker border
    display.fillRoundRect(bx, by, bw, bh, r, GxEPD_WHITE);
    display.drawRoundRect(bx, by, bw, bh, r, GxEPD_BLACK);
    if (r > 1) display.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, r - 1, GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);
  }

  display.setFont();
  display.setTextSize(2);
  // Center text vertically inside the button
  printCenteredBoldDefault(label, by + (bh - 16) / 2);

  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
}


static void timeEditIncrementDigit(uint8_t digitIndex) {
  if (timeEditMinutesOnly) {
    // Minutes-only (00..999), seconds hidden & forced to 00
    uint16_t minutes = timeEditSec / 60;
    if (minutes > 999) minutes = 999;

    if (digitIndex >= timeEditDigitCount) return;

    uint8_t d0, d1, d2;
    if (timeEditDigitCount == 3) {
      d0 = (minutes / 100) % 10;
      d1 = (minutes / 10) % 10;
      d2 = minutes % 10;
      if (digitIndex == 0) d0 = (d0 + 1) % 10;
      if (digitIndex == 1) d1 = (d1 + 1) % 10;
      if (digitIndex == 2) d2 = (d2 + 1) % 10;
      minutes = (uint16_t)(d0 * 100 + d1 * 10 + d2);
    } else {
      d0 = (minutes / 10) % 10;
      d1 = minutes % 10;
      if (digitIndex == 0) d0 = (d0 + 1) % 10;
      if (digitIndex == 1) d1 = (d1 + 1) % 10;
      minutes = (uint16_t)(d0 * 10 + d1);
    }

    timeEditSec = (uint16_t)(minutes * 60);
    clampOrWrapTimeEdit();
    return;
  }

  // MM:SS editor (sport)
  uint16_t mm = timeEditSec / 60;
  uint16_t ss = timeEditSec % 60;
  if (mm > 99) mm = 99;

  uint8_t mT = (mm / 10) % 10;
  uint8_t mO = mm % 10;
  uint8_t sT = (ss / 10) % 6;
  uint8_t sO = ss % 10;

  switch (digitIndex) {
    case 0: mT = (mT + 1) % 10; break;
    case 1: mO = (mO + 1) % 10; break;
    case 2: sT = (sT + 1) % 6;  break;
    case 3: sO = (sO + 1) % 10; break;
    default: break;
  }

  mm = (uint16_t)(mT * 10 + mO);
  ss = (uint16_t)(sT * 10 + sO);

  timeEditSec = (uint16_t)(mm * 60 + ss);
  clampOrWrapTimeEdit();
}

static void drawTimeEditScreen() {
  display.fillScreen(GxEPD_WHITE);
  drawBatteryIcon();

  display.setTextColor(GxEPD_BLACK);
  printCentered(String(timeEditTitle), 25, &FreeSansBold9pt7b);
  display.drawLine(20, 30, 180, 30, GxEPD_BLACK);

  // Big built-in font => easy highlight with predictable char size
  display.setFont();
  display.setTextSize(4);

  const int16_t charW = 6 * 4;
  const int16_t charH = 8 * 4;

  uint8_t selected = timeEditFieldOrder[timeEditFieldIndex];
  bool valSel = (selected == TE_FIELD_VALIDATE);

  if (timeEditMinutesOnly) {
    // ----- MINUTES ONLY -----
    // Small label (built-in font) for better readability on e-paper
    display.setFont();
    display.setTextSize(1);
    printCentered("Minutes", 55);
    display.setTextSize(4);

    uint16_t minutes = timeEditSec / 60;
    if (minutes > 999) minutes = 999;

    char buf[5] = {0};
    if (timeEditDigitCount == 3) snprintf(buf, sizeof(buf), "%03u", (unsigned)minutes);
    else snprintf(buf, sizeof(buf), "%02u", (unsigned)minutes);

    const int16_t totalW = charW * (int16_t)timeEditDigitCount;
    const int16_t startX = (SCREEN_W - totalW) / 2;
    const int16_t baseY  = 85; // top Y for built-in font

    int selCharIdx = -1;
    if (!valSel && selected < timeEditDigitCount) selCharIdx = selected;

    for (int i = 0; i < timeEditDigitCount; i++) {
      int16_t x = startX + i * charW;
      int16_t yTop = baseY;

      bool hl = (i == selCharIdx);
      if (hl) {
        int16_t ulY = yTop + charH + 2;
        display.drawLine(x, ulY, x + charW - 1, ulY, GxEPD_BLACK);
        display.drawLine(x, ulY + 1, x + charW - 1, ulY + 1, GxEPD_BLACK);
      }

      display.setTextColor(GxEPD_BLACK);
      display.setCursor(x, baseY);
      display.print(buf[i]);
    }

    // Valider button (same style as START)
    drawActionButton("Valider", 140, valSel);

    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    printCentered("Click: + | Hold: Next", 190);
    return;
  }

  // ----- MM:SS -----
  const int16_t totalW = charW * 5; // "MM:SS" => 5 chars
  const int16_t startX = (SCREEN_W - totalW) / 2;
  const int16_t baseY  = 85;

  // Small label above the timer
  display.setFont();
  display.setTextSize(1);
  printCentered("MM:SS", 55);
  display.setTextSize(4);


  // Format "MM:SS"
  uint16_t mm = timeEditSec / 60;
  uint16_t ss = timeEditSec % 60;
  if (mm > 99) mm = 99;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)mm, (unsigned)ss);

  int selCharIdx = -1;
  if (!valSel) {
    if (selected == 0) selCharIdx = 0;
    else if (selected == 1) selCharIdx = 1;
    else if (selected == 2) selCharIdx = 3;
    else if (selected == 3) selCharIdx = 4;
  }

  for (int i = 0; i < 5; i++) {
    int16_t x = startX + i * charW;
    int16_t yTop = baseY;

    bool hl = (i == selCharIdx);
    if (buf[i] == ':') hl = false;

    if (hl) {
      int16_t ulY = yTop + charH + 2;
      display.drawLine(x, ulY, x + charW - 1, ulY, GxEPD_BLACK);
      display.drawLine(x, ulY + 1, x + charW - 1, ulY + 1, GxEPD_BLACK);
    }

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x, baseY);
    display.print(buf[i]);
  }

  drawActionButton("Valider", 140, valSel);

  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  printCentered("Click: + | Hold: Next", 190);
}


void drawReikiSessionInterface(uint32_t elapsedSec) {
  display.fillScreen(GxEPD_WHITE);
  
  uint16_t intDur = getReikiIntervalSec();
  uint32_t currentInt = elapsedSec / intDur;
  if (currentInt >= reikiNumIntervals) currentInt = reikiNumIntervals - 1;
  uint32_t intElapsed = elapsedSec - (currentInt * intDur);
  uint32_t intRemaining = intDur - intElapsed;

  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2;
  
  display.setTextColor(GxEPD_BLACK);
  String posStr = "POSITION " + String(currentInt + 1) + "/" + String(reikiNumIntervals);
  printCentered(posStr, 40, &FreeSansBold9pt7b);

  printCentered(formatTimeMMSS(intRemaining), cy + 10, &FreeSansBold18pt7b);

  int barW = 160;
  int barH = 10;
  int barX = (SCREEN_W - barW) / 2;
  int barY = 140;
  
  display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
  int fillW = map(intElapsed, 0, intDur, 0, barW);
  display.fillRect(barX, barY, fillW, barH, GxEPD_BLACK);
}

void drawTimerCommon(String centerText, String subText, float progress) {
  display.fillScreen(GxEPD_WHITE);
  drawBatteryIcon(); 
  
  printCentered(centerText, 100, &FreeSansBold18pt7b);
  printCentered(subText, 140);
  
  display.drawRect(10, 160, 180, 8, GxEPD_BLACK);
  display.fillRect(10, 160, (int)(180 * progress), 8, GxEPD_BLACK);

  printCentered("Long Press: STOP", 190);
}

// --- SPORT INTERVAL TIMER ---

uint32_t getSportTotalSec() {
  if (sportRounds <= 1) return (uint32_t)sportWorkSec;
  return (uint32_t)sportRounds * sportWorkSec + (uint32_t)(sportRounds - 1) * sportRestSec;
}

// Calcule la phase/round/temps restant à partir de elapsedSec.
// Note: pas de repos après le dernier effort.
bool computeSport(uint32_t elapsedSec, SportPhase &phase, uint8_t &round, uint16_t &phaseRemaining, uint32_t &totalSec) {
  totalSec = getSportTotalSec();
  if (elapsedSec >= totalSec) return false;

  if (sportRounds <= 1) {
    phase = SPORT_WORK;
    round = 1;
    phaseRemaining = (uint16_t)(sportWorkSec - elapsedSec);
    return true;
  }

  uint32_t cycle = (uint32_t)sportWorkSec + (uint32_t)sportRestSec; // pour les rounds 1..N-1
  uint32_t preTotal = (uint32_t)(sportRounds - 1) * cycle;

  if (elapsedSec < preTotal) {
    uint32_t idx = elapsedSec / cycle;      // 0..N-2
    uint32_t inR = elapsedSec % cycle;
    round = (uint8_t)(idx + 1);
    if (inR < sportWorkSec) {
      phase = SPORT_WORK;
      phaseRemaining = (uint16_t)(sportWorkSec - inR);
    } else {
      phase = SPORT_REST;
      phaseRemaining = (uint16_t)(sportRestSec - (inR - sportWorkSec));
    }
    return true;
  } else {
    // dernier round: effort uniquement
    uint32_t inLast = elapsedSec - preTotal;
    round = sportRounds;
    phase = SPORT_WORK;
    phaseRemaining = (uint16_t)(sportWorkSec - inLast);
    return true;
  }
}

void drawSportSessionInterface(uint32_t elapsedSec) {
  SportPhase phase;
  uint8_t round;
  uint16_t phaseRemain;
  uint32_t totalSec;

  if (!computeSport(elapsedSec, phase, round, phaseRemain, totalSec)) {
    // fini
    display.fillScreen(GxEPD_WHITE);
    printCentered("SPORT", 60, &FreeSansBold18pt7b);
    printCentered("Termine", 120, &FreeSansBold18pt7b);
    printCentered("Long Press: STOP", 190);
    return;
  }

  String phaseTxt = (phase == SPORT_WORK) ? "WORK" : "REST";
  String sub = phaseTxt + "  R" + String(round) + "/" + String(sportRounds);
  String center = formatTimeMMSS(phaseRemain);

  drawTimerCommon(center, sub, (float)elapsedSec / (float)totalSec);
}
// --- FOCUS (Pomodoro) ---

uint32_t getFocusTotalSec() {
  // Pas de pause après le dernier work
  if (focusSets <= 1) return (uint32_t)focusWorkSec;
  return (uint32_t)focusSets * (uint32_t)focusWorkSec
       + (uint32_t)(focusSets - 1) * (uint32_t)focusBreakSec;
}

// Calcule phase / set courant / temps restant à partir de elapsedSec.
bool computeFocus(uint32_t elapsedSec, FocusPhase &phase, uint8_t &setIdx, uint16_t &phaseRemaining, uint32_t &totalSec) {
  totalSec = getFocusTotalSec();
  if (elapsedSec >= totalSec) return false;

  uint32_t t = elapsedSec;
  for (uint8_t s = 1; s <= focusSets; s++) {
    uint32_t w = (uint32_t)focusWorkSec;
    if (t < w) {
      phase = FOCUS_WORK;
      setIdx = s;
      phaseRemaining = (uint16_t)(w - t);
      return true;
    }
    t -= w;

    if (s == focusSets) break; // pas de pause après le dernier
    uint32_t b = (uint32_t)focusBreakSec;
    if (t < b) {
      phase = FOCUS_BREAK;
      setIdx = s; // pause après le set s
      phaseRemaining = (uint16_t)(b - t);
      return true;
    }
    t -= b;
  }
  return false;
}

void drawFocusSessionInterface(uint32_t elapsedSec) {
  FocusPhase phase;
  uint8_t setIdx;
  uint16_t phaseRemain;
  uint32_t totalSec;

  if (!computeFocus(elapsedSec, phase, setIdx, phaseRemain, totalSec)) {
    display.fillScreen(GxEPD_WHITE);
    printCentered("FOCUS", 60, &FreeSansBold18pt7b);
    printCentered("Termine", 120, &FreeSansBold18pt7b);
    printCentered("Long Press: STOP", 190);
    return;
  }

  String phaseTxt = (phase == FOCUS_WORK) ? "WORK" : "BREAK";
  String sub = phaseTxt + "  P" + String(setIdx) + "/" + String(focusSets);
  String center = formatTimeMMSS(phaseRemain);

  drawTimerCommon(center, sub, (float)elapsedSec / (float)totalSec);
}
void drawDoneScreen() {
  display.fillScreen(GxEPD_WHITE);
  drawBatteryIcon();

  display.setTextColor(GxEPD_BLACK);
  printCentered("TERMINE", 70, &FreeSansBold18pt7b);
  printCentered("Bravo !", 105, &FreeSansBold9pt7b);
  printCentered("Click: retour", 190);
}





void refreshScreen() {
  bool partial = !uiFullRefresh; 
  uiFullRefresh = false; 

  switch(appState) {
    case STATE_MODE_SELECT:
      drawModeSelectScreen();
      break;

    
case STATE_REIKI_CONFIG: {
  String items[] = {
    "Positions: " + String(reikiNumIntervals),
    "Temps: " + formatTimeMMSS(reikiIntervalSec),
    "START"
  };
  drawGenericConfig("REIKI SETUP", reikiMenuIndex, 3, items);
  break;
}

    case STATE_MEDIT_CONFIG: {
      String items[] = {
        "Duree: " + formatTimeMMSS(meditTotalSec),
        "Mi-temps: " + String(meditationMidAlert ? "ON" : "OFF"),
        "START"
      };
      drawGenericConfig("MEDIT SETUP", meditMenuIndex, 3, items);
    } break;

    case STATE_SPORT_CONFIG: {
      String items[] = {
        "Work: " + formatTimeMMSS(sportWorkSec),
        "Rest: " + formatTimeMMSS(sportRestSec),
        "Rounds: " + String(sportRounds),
        "START"
      };
      drawGenericConfig("SPORT SETUP", sportMenuIndex, 4, items);
    } break;

    case STATE_FOCUS_CONFIG: {
      String items[] = {
        "Work: " + formatTimeMMSS(focusWorkSec),
        "Break: " + formatTimeMMSS(focusBreakSec),
        "Sets: " + String(focusSets),
        "START"
      };
      drawGenericConfig("FOCUS SETUP", focusMenuIndex, 4, items);
    } break;

    case STATE_TIME_EDIT:
      drawTimeEditScreen();
      break;

    case STATE_REIKI_RUNNING: {
      uint32_t elapsed = (millis() - reikiSessionStartMs) / 1000;
      drawReikiSessionInterface(elapsed);
    } break;

    case STATE_DONE:
      drawDoneScreen();
      break;

    case STATE_MEDIT_RUNNING: {
      uint32_t elapsed = (millis() - meditSessionStartMs) / 1000;
      uint32_t total   = getMeditTotalSec();
      if (elapsed > total) elapsed = total;
      drawTimerCommon(formatTimeMMSS(total - elapsed), "Restant", (float)elapsed / total);
    } break;

    case STATE_SPORT_RUNNING: {
      uint32_t elapsed = (millis() - sportSessionStartMs) / 1000;
      drawSportSessionInterface(elapsed);
    } break;

    case STATE_FOCUS_RUNNING: {
      uint32_t elapsed = (millis() - focusSessionStartMs) / 1000;
      drawFocusSessionInterface(elapsed);
    } break;
  }


  if (partial) display.updateWindow(0, 0, SCREEN_W, SCREEN_H, true);
  else display.update();
}

// -----------------------------------------------------------------------------
// DEEP SLEEP (CORRECTIF IMAGE GRISE)
// -----------------------------------------------------------------------------
static void epdDrawScrubPattern() {
  // Pattern léger pour "casser" la rémanence après beaucoup de partial refresh
  display.fillScreen(GxEPD_WHITE);
  for (int16_t y = 0; y < SCREEN_H; y += 2) {
    display.drawFastHLine(0, y, SCREEN_W, GxEPD_BLACK);
  }
}

void goToDeepSleep() {
  // 0) Stop vibrations
  ledcWrite(MOTOR_PWM_CH, 0);

  // 1) Nettoyage e-paper avant l’image de veille (anti “image fantôme”)
  // 2 cycles suffisent en général. Monte à 3 si tu vois encore des traces après une session.
  // IMPORTANT: on force un "plein écran" si ta lib le supporte (GxEPD2).
  const uint8_t CLEAN_CYCLES = 2;
  for (uint8_t i = 0; i < CLEAN_CYCLES; i++) {
    // Séquence WHITE->BLACK->WHITE plus efficace contre la rémanence
    display.fillScreen(GxEPD_WHITE);
    display.update();

    display.fillScreen(GxEPD_BLACK);
    display.update();

    display.fillScreen(GxEPD_WHITE);
    display.update();

    // Petit pattern pour casser les pixels "coincés" (utile après beaucoup de partial)
    epdDrawScrubPattern();
    display.update();

    display.fillScreen(GxEPD_WHITE);
    display.update();
  }

  // 2) Image de veille sur fond blanc
  display.fillScreen(GxEPD_WHITE);
  int16_t x = (SCREEN_W - 128) / 2;
  int16_t y = (SCREEN_H - 128) / 2;
  display.drawBitmap(x, y, bitmap_logo_sleep, 128, 128, GxEPD_BLACK);
  display.update(); // full refresh final

  delay(200);

  // 3) Mettre le contrôleur e-paper en sommeil "propre"
  display.powerDown(); // ou display.hibernate() selon ton build GxEPD

  // 4) GPIO dans un état sûr
  // IMPORTANT : CS = HIGH (désélection)
  pinMode(EPD_CS, OUTPUT);
  digitalWrite(EPD_CS, HIGH);

  // IMPORTANT : RESET pas à LOW
  pinMode(EPD_RESET, OUTPUT);
  digitalWrite(EPD_RESET, HIGH);

  // Lignes SPI/DC stables
  pinMode(SPI_SCK, OUTPUT); digitalWrite(SPI_SCK, LOW);
  pinMode(SPI_DIN, OUTPUT); digitalWrite(SPI_DIN, LOW);
  pinMode(EPD_DC,  OUTPUT); digitalWrite(EPD_DC, LOW);

  // Hold uniquement sur états sûrs
  gpio_hold_en((gpio_num_t)EPD_CS);
  gpio_hold_en((gpio_num_t)EPD_RESET);
  gpio_hold_en((gpio_num_t)SPI_SCK);
  gpio_hold_en((gpio_num_t)SPI_DIN);
  gpio_hold_en((gpio_num_t)EPD_DC);

  // 5) Power rails (selon ton besoin)
  pinMode(PWR_EN, OUTPUT);
  digitalWrite(PWR_EN, HIGH);
  gpio_hold_en((gpio_num_t)PWR_EN);

  // Backlight OFF (si ton modèle en a)
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, HIGH);
  gpio_hold_en((gpio_num_t)BACKLIGHT);

  gpio_deep_sleep_hold_en();

  // 6) Wakeup bouton
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);

  esp_deep_sleep_start();
}


// -----------------------------------------------------------------------------
// LOGIQUE START/STOP
// -----------------------------------------------------------------------------
void startReiki() {
  reikiRunning = true;
  appState = STATE_REIKI_RUNNING;
  lastMode = MODE_REIKI;
  reikiSessionStartMs = millis();
  triggerVibePattern(2);
  requestUi(true); 
}

void startMedit() {
  meditRunning = true;
  appState = STATE_MEDIT_RUNNING;
  lastMode = MODE_MEDIT;
  meditMidAlertDone = false;
  meditSessionStartMs = millis();
  triggerVibePattern(2);
  requestUi(true);
}

void startSport() {
  sportRunning = true;
  appState = STATE_SPORT_RUNNING;
  lastMode = MODE_SPORT;
  sportLastPhase = 255;
  sportLastRound = 0;
  sportSessionStartMs = millis();
  triggerVibePattern(2);
  requestUi(true);
}
void startFocus() {
  focusRunning = true;
  appState = STATE_FOCUS_RUNNING;
  lastMode = MODE_FOCUS;
  focusLastPhase = 255;
  focusLastSet = 0;
  focusSessionStartMs = millis();
  triggerVibePattern(2);
  requestUi(true);
}




void stopSession(bool finished) {
  reikiRunning = false;
  meditRunning = false;
  sportRunning = false;
  focusRunning = false;

  if (finished) {
    triggerVibePattern(4);
    appState = STATE_DONE;
  } else {
    triggerVibePattern(1);
    if (lastMode == MODE_REIKI) appState = STATE_REIKI_CONFIG;
    else if (lastMode == MODE_MEDIT) appState = STATE_MEDIT_CONFIG;
    else if (lastMode == MODE_SPORT) appState = STATE_SPORT_CONFIG;
    else appState = STATE_FOCUS_CONFIG;
  }
  requestUi(true);
}

// -----------------------------------------------------------------------------
// INPUT
// -----------------------------------------------------------------------------
void handleEvent(AceButton *, uint8_t eventType, uint8_t) {
  resetSleepTimer(); 
  
  switch (eventType) {
    case AceButton::kEventClicked: 
      triggerVibePattern(1);
      if (appState == STATE_TIME_EDIT) {
        uint8_t code = timeEditFieldOrder[timeEditFieldIndex];
        if (code == TE_FIELD_VALIDATE) {
          applyTimeEditValue();
          appState = timeEditReturnState;
        } else {
          timeEditIncrementDigit(code);
        }
        requestUi(true);
        return;
      }
      if (appState == STATE_MODE_SELECT) {
        modeSelectHighlight = (Mode)((modeSelectHighlight + 1) % 4);
        requestUi();
      } 
      else if (appState == STATE_REIKI_CONFIG) {
        reikiMenuIndex = (reikiMenuIndex + 1) % 3;
        requestUi();
      } 
      else if (appState == STATE_MEDIT_CONFIG) {
        meditMenuIndex = (meditMenuIndex + 1) % 3;
        requestUi();
      }
      else if (appState == STATE_SPORT_CONFIG) {
        sportMenuIndex = (sportMenuIndex + 1) % 4;
        requestUi();
      }
      else if (appState == STATE_FOCUS_CONFIG) {
        focusMenuIndex = (focusMenuIndex + 1) % 4;
        requestUi();
      } 
      else if (appState == STATE_DONE) {
        if (lastMode == MODE_REIKI) appState = STATE_REIKI_CONFIG;
    else if (lastMode == MODE_MEDIT) appState = STATE_MEDIT_CONFIG;
    else if (lastMode == MODE_SPORT) appState = STATE_SPORT_CONFIG;
    else appState = STATE_FOCUS_CONFIG;
        requestUi(true);
      }
      break;

    case AceButton::kEventLongPressed: 
      triggerVibePattern(1); 
      if (appState == STATE_TIME_EDIT) {
        timeEditFieldIndex = (timeEditFieldIndex + 1) % timeEditFieldCount;
        requestUi(true);
        return;
      }
      if (appState == STATE_MODE_SELECT) {
        currentMode = modeSelectHighlight;
        if (currentMode == MODE_MEDIT) appState = STATE_MEDIT_CONFIG;
        else if (currentMode == MODE_REIKI) appState = STATE_REIKI_CONFIG;
        else if (currentMode == MODE_SPORT) appState = STATE_SPORT_CONFIG;
        else appState = STATE_FOCUS_CONFIG;
        requestUi(true);
      }
      
else if (appState == STATE_REIKI_CONFIG) {
  if (reikiMenuIndex == 0) {
    reikiNumIntervals++;
    if (reikiNumIntervals > 15) reikiNumIntervals = 1;
  }
  else if (reikiMenuIndex == 1) {
    // Edition du temps (minutes)
    enterTimeEdit(TE_REIKI_INTERVAL, "REIKI", reikiIntervalSec, 60, 60 * 60, true, STATE_REIKI_CONFIG);
    return;
  }
  else if (reikiMenuIndex == 2) {
    startReiki();
    return;
  }
  requestUi(true);
}
      
else if (appState == STATE_MEDIT_CONFIG) {
  if (meditMenuIndex == 0) {
    enterTimeEdit(TE_MEDIT_TOTAL, "MEDIT", meditTotalSec, 60, 60 * 60, true, STATE_MEDIT_CONFIG);
    return;
  }
  else if (meditMenuIndex == 1) {
    meditationMidAlert = !meditationMidAlert;
  }
  else if (meditMenuIndex == 2) {
    startMedit();
    return;
  }
  requestUi(true);
}
      
else if (appState == STATE_SPORT_CONFIG) {
  if (sportMenuIndex == 0) { // Work
    enterTimeEdit(TE_SPORT_WORK, "WORK", sportWorkSec, 5, 99 * 60 + 59, false, STATE_SPORT_CONFIG);
    return;
  }
  else if (sportMenuIndex == 1) { // Rest
    enterTimeEdit(TE_SPORT_REST, "REST", sportRestSec, 5, 99 * 60 + 59, false, STATE_SPORT_CONFIG);
    return;
  }
  else if (sportMenuIndex == 2) { // Rounds
    sportRounds++;
    if (sportRounds > 30) sportRounds = 1;
  }
  else if (sportMenuIndex == 3) {
    startSport();
    return;
  }
  requestUi(true);
}


else if (appState == STATE_FOCUS_CONFIG) {
  if (focusMenuIndex == 0) { // Work
    enterTimeEdit(TE_FOCUS_WORK, "FOCUS WORK", focusWorkSec, 5 * 60, 60 * 60, true, STATE_FOCUS_CONFIG);
    return;
  }
  else if (focusMenuIndex == 1) { // Break
    enterTimeEdit(TE_FOCUS_BREAK, "FOCUS BREAK", focusBreakSec, 60, 60 * 60, true, STATE_FOCUS_CONFIG);
    return;
  }
  else if (focusMenuIndex == 2) { // Sets
    focusSets++;
    if (focusSets > 12) focusSets = 1;
  }
  else if (focusMenuIndex == 3) {
    startFocus();
    return;
  }
  requestUi(true);
}

else if (appState == STATE_REIKI_RUNNING || appState == STATE_MEDIT_RUNNING || appState == STATE_SPORT_RUNNING || appState == STATE_FOCUS_RUNNING) {
        stopSession(false);
      }
      break;
  }
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Deep-sleep GPIO hold release (IMPORTANT):
// On ESP32, if we enabled gpio_hold_en() + gpio_deep_sleep_hold_en() before deep sleep,
// we MUST release these holds after wake, otherwise EPD/SPI pins can stay frozen and
// the display will not update.
// -----------------------------------------------------------------------------
static void releaseDeepSleepHolds() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)EPD_CS);
  gpio_hold_dis((gpio_num_t)EPD_DC);
  gpio_hold_dis((gpio_num_t)EPD_RESET);
  gpio_hold_dis((gpio_num_t)EPD_BUSY);
  gpio_hold_dis((gpio_num_t)SPI_SCK);
  gpio_hold_dis((gpio_num_t)SPI_DIN);
  gpio_hold_dis((gpio_num_t)PWR_EN);
  gpio_hold_dis((gpio_num_t)BACKLIGHT);
}

void setup() {
  releaseDeepSleepHolds();
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC, ADC_11db);
  
  gpio_hold_dis((gpio_num_t)BACKLIGHT); 
  gpio_hold_dis((gpio_num_t)PWR_EN);

  setCpuFrequencyMhz(80); 
  Serial.begin(115200);

  ledcSetup(MOTOR_PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_MOTOR, MOTOR_PWM_CH);
  ledcWrite(MOTOR_PWM_CH, 0);

  pinMode(PWR_EN, OUTPUT);
  digitalWrite(PWR_EN, HIGH); 
  
  pinMode(PIN_KEY, INPUT_PULLUP); 
  pinMode(BAT_ADC, INPUT);

  SPI.begin(SPI_SCK, -1, SPI_DIN, EPD_CS);
  display.init(115200); 
  display.setRotation(1); 
  
    // Au réveil deep sleep, on revient au menu principal
  appState = STATE_MODE_SELECT;
  modeSelectHighlight = currentMode;

  ButtonConfig *cfg = button.getButtonConfig();
  cfg->setEventHandler(handleEvent);
  cfg->setFeature(ButtonConfig::kFeatureClick);
  cfg->setFeature(ButtonConfig::kFeatureLongPress);
  cfg->setLongPressDelay(600); 

  xTaskCreatePinnedToCore(buttonTask, "Btn", 2048, NULL, 1, NULL, 0);

  // Vibration progressive au démarrage
  for (int i = 30; i <= 80; i += 5) { 
    ledcWrite(MOTOR_PWM_CH, i);
    delay(20);
  }
  ledcWrite(MOTOR_PWM_CH, 0);

  requestUi(true);
}

void loop() {
  
  if (uiNeedsUpdate) {
    refreshScreen(); 
  }

  unsigned long now = millis();

  if (appState == STATE_REIKI_RUNNING) {
    resetSleepTimer();
    uint32_t elapsed = (now - reikiSessionStartMs) / 1000;
    uint16_t intDur  = getReikiIntervalSec();
    uint32_t total   = reikiNumIntervals * intDur;

    if (elapsed >= total) {
      stopSession(true);
    } else {
      uint8_t currentInt = elapsed / intDur;
      static uint8_t lastInt = 0;
      if (currentInt > lastInt) {
        triggerVibePattern(3); 
        lastInt = currentInt;
      }
      
      if (elapsed != reikiLastDrawSec) {
        reikiLastDrawSec = elapsed;
        requestUi(false); 
      }
    }
  } 
  else if (appState == STATE_MEDIT_RUNNING) {
    resetSleepTimer();
    uint32_t elapsed = (now - meditSessionStartMs) / 1000;
    uint32_t total   = getMeditTotalSec();

    if (elapsed >= total) {
      stopSession(true);
    } else {
      if (meditationMidAlert && !meditMidAlertDone && elapsed >= total/2) {
        meditMidAlertDone = true;
        triggerVibePattern(5); 
      }
      if (elapsed != meditLastDrawSec) {
        meditLastDrawSec = elapsed;
        requestUi(false); 
      }
    }
  } 

  else if (appState == STATE_SPORT_RUNNING) {
    resetSleepTimer();
    uint32_t elapsed = (now - sportSessionStartMs) / 1000;
    uint32_t total   = getSportTotalSec();

    if (elapsed >= total) {
      stopSession(true);
    } else {
      // Vibes sur changement de phase / round
      SportPhase phase;
      uint8_t round;
      uint16_t phaseRemain;
      uint32_t total2;
      if (computeSport(elapsed, phase, round, phaseRemain, total2)) {
        if (sportLastPhase == 255) {
          sportLastPhase = (uint8_t)phase;
          sportLastRound = round;
        } else {
          if ((uint8_t)phase != sportLastPhase || round != sportLastRound) {
            if (phase == SPORT_WORK) triggerVibePattern(2);   // effort
            else triggerVibePattern(1);                       // repos
            sportLastPhase = (uint8_t)phase;
            sportLastRound = round;
          }
        }
      }

      if (elapsed != sportLastDrawSec) {
        sportLastDrawSec = elapsed;
        requestUi(false);
      }
    }
  }
  else if (appState == STATE_FOCUS_RUNNING) {
    resetSleepTimer();
    uint32_t elapsed = (now - focusSessionStartMs) / 1000;
    uint32_t total   = getFocusTotalSec();

    if (elapsed >= total) {
      stopSession(true);
    } else {
      // Vibes sur changement de phase / set
      FocusPhase phase;
      uint8_t setIdx;
      uint16_t phaseRemain;
      uint32_t total2;
      if (computeFocus(elapsed, phase, setIdx, phaseRemain, total2)) {
        if (focusLastPhase == 255) {
          focusLastPhase = (uint8_t)phase;
          focusLastSet = setIdx;
        } else {
          if ((uint8_t)phase != focusLastPhase || setIdx != focusLastSet) {
            if (phase == FOCUS_WORK) triggerVibePattern(2);  // reprise travail
            else triggerVibePattern(1);                      // pause
            focusLastPhase = (uint8_t)phase;
            focusLastSet = setIdx;
          }
        }
      }

      if (elapsed != focusLastDrawSec) {
        focusLastDrawSec = elapsed;
        requestUi(false);
      }
    }
  }



  else {
    if (now - lastInteractionTime > SLEEP_TIMEOUT_MS) {
      goToDeepSleep();
    }
  }
  
  delay(10);
}