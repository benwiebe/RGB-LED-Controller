#include <EEPROM.h>

#define USE_TIMER 2
#include <MD_PWM.h>

/* Configuration Constants - Edit these */
const int R_PIN = 10;
const int G_PIN = 9;
const int B_PIN = 11;

const char CHAR_SEP = ','; // pattern separator

const int SERIAL_BAUD = 9600;
const int PWM_FREQ = 250; // Hz

#define DEBUG 1 // set to 1 to enable debugging logs

/* Constants - Do not edit! */
const unsigned int MAX_INT = 65535;
// Max for EEPROM is 170, artificially limiting to lower to allow for more local RAM usage
#define MAX_PATTERN_LEN 50 // Needs to be a macro to make the compiler happy

const unsigned int META_PATTERN_SAVED = 0x1;

const unsigned int EEPROM_META_CELL = 0;
const unsigned int EEPROM_MODE_CELL = 1;
const unsigned int EEPROM_DATA_START = 4;

// Static Timer Math, this will get optimized at compile-time
const unsigned int TIMER_TICK_US = 64; // for 1024 prescale
const unsigned int US_PER_MS = 1000;
const float TIMER_TICKS_PER_MS = (float) US_PER_MS / TIMER_TICK_US;
const double MAX_MS_BEFORE_OVERFLOW = MAX_INT / TIMER_TICKS_PER_MS; // truncated, ie. rounded-down
const long TIMER_TICKS_MAX = (long) floor(MAX_MS_BEFORE_OVERFLOW * TIMER_TICKS_PER_MS);

const long REMAINDER_FUDGE_TICKS = 10 * TIMER_TICKS_PER_MS; // 10 millis

// Demo Patterns
const char DEMO1[] PROGMEM = "J,FF0000,2000,00FF00,2000,0000FF";
const char* const DEMO_PATTERNS[] PROGMEM = {
  DEMO1
};

/* Helper Macros */
#if DEBUG == 1
  #define debugPrint(...) Serial.print(__VA_ARGS__)
  #define debugPrintln(...) Serial.println(__VA_ARGS__)
#else
  #define debugPrint(...)
  #define debugPrintln(...)
#endif

/* Globals */
MD_PWM pwm[3] = {MD_PWM(R_PIN), MD_PWM(G_PIN), MD_PWM(B_PIN)};

bool useTimerCount = false;
unsigned int timerTarget = 0;
unsigned int timerCount = 0;

char mode = 'X';
unsigned int patternLength = 0;
unsigned long patternColours[MAX_PATTERN_LEN];
unsigned int patternDelay[MAX_PATTERN_LEN];
unsigned int patternIndex = 0;

/* Standard Arduino Methods */

void setup() {
  // Disable Interrupts until we're ready, then setup the timer
  cli();

  // Enable serial communication
  Serial.begin(SERIAL_BAUD);

  // Setup PWM
  for (uint8_t i = 0; i < 3; i++)
  {
    if (!pwm[i].begin(PWM_FREQ))
    {
      Serial.print("\nUnable to initialize pwm #");
      Serial.println(i, DEC);
    }
  }

  setupTimer();
  Serial.println("== Setup Complete ==");

  // Load any saved pattern
  loadPattern();
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readString();
    input = input.substring(0, input.length()-1); //drop newline char
    Serial.print("> ");
    Serial.println(input);
    disableTimerInterrupts();
    if (parse(input)) {
      Serial.println("OK");
      if (mode != 'X') {
        resetTimer(true);
        displayInitial();
      }
    } else {
      Serial.println("ERROR");
    }
    if (mode != 'X') {
      enableTimerInterrupts();
    }
  }
}

/* Parser Methods */
bool parse(String input) {
  char mode = input[0];

  switch (mode) {
    case 'S':
      return parseStatic(input);
    case 'J':
      return parsePattern(input);
    case 'F':
      Serial.println("Mode F not implemented");
      break;
    case 'D':
      return demoPattern(input);
    case 'C':
      savePattern();
      return true;
    case 'R':
      resetEeprom();
      return true;
    case 'H':
      printHelp();
      return true;
  }
  return false;
}

bool parseStatic(String input) {
  String colourHex = input.substring(2, 9);
  long colour = hexToLong(colourHex);
  if (colour < 0) {
    return false;
  }
  patternLength = 1;
  patternColours[0] = colour;
  patternDelay[0] = 10000;
  mode = 'S';
  return true;
}

bool parsePattern(String input) {
  String copy = input.substring(2);
  if (copy[copy.length() - 1] != ',') {
    copy += ',';
  }
  int idx = 0;
  while (copy.indexOf(CHAR_SEP) > -1) {
    String term = copy.substring(0, copy.indexOf(CHAR_SEP));
    if (idx % 2 == 0) {
      patternColours[idx / 2] = hexToLong(term);
      debugPrint("colour: ");
      debugPrintln(patternColours[idx/2], 16);
    } else {
      patternDelay[(idx - 1) / 2] = term.toInt();
      debugPrint("delay: ");
      debugPrintln(patternDelay[(idx-1)/2], 10);
    }
    idx++;
    if (idx/2 >= MAX_PATTERN_LEN) {
      Serial.print("PATTERN TOO LONG, MAX:");
      Serial.println(MAX_PATTERN_LEN, DEC);
      mode = 'X';
      return false;
    }
    copy = copy.substring(copy.indexOf(CHAR_SEP) + 1);
  }
  patternLength = idx/2;
  mode = input[0];
  return true;
}

bool demoPattern(String input) {
  int demoIdx = input.substring(1).toInt();
  if (demoIdx < sizeof(DEMO_PATTERNS)) {
    return parse(DEMO_PATTERNS[demoIdx]);
  }
  return false;
}

unsigned long hexToLong(String hex) {
  unsigned long result = 0;
  for (int i=0; i<hex.length(); i++) {
    unsigned long res = -1;
    char hChar = hex.charAt(i);
    if (hChar >= 48 && hChar <= 57) {
      res = hChar - 48;
    } else if (hChar >= 65 && hChar <= 70) {
      res = hChar - 65 + 0xA;
    } else if (hChar >= 97 && hChar <= 102) {
      res = hChar - 97 + 0xA;
    } else {
      return -1;
    }
    result = result | (res << (20 - (4 * i)));
  }
  return result;
}

/* Pattern Display Methods */
void displayInitial() {
  patternIndex = 0;
  display();
}

void displayNext() {
  if (patternLength > 1) {
    patternIndex = (patternIndex + 1) % patternLength;
  }
  display();
}

void display() {
  debugPrintln("DISPLAY");
  unsigned long colour = patternColours[patternIndex];
  int red = (colour & 0xFF0000) / 0x10000;
  int green = (colour & 0x00FF00) / 0x100;
  int blue = (colour & 0x0000FF);

  debugPrint(red, DEC);
  debugPrint(",");
  debugPrint(green, DEC);
  debugPrint(",");
  debugPrint(blue, DEC);
  debugPrintln("");

  pwm[0].write(red);
  pwm[1].write(green);
  pwm[2].write(blue);

  setTimer(patternDelay[patternIndex]);
}

/* EEPROM Methods */
void savePattern() {
  byte meta = META_PATTERN_SAVED;
  EEPROM.put(EEPROM_META_CELL, meta);

  EEPROM.put(EEPROM_MODE_CELL, mode);

  for (int i = 0; i < MAX_PATTERN_LEN; i++) {
    EEPROM.put(EEPROM_DATA_START + (i * sizeof(long)), patternColours[i]);
  }

  for (int i = 0; i < MAX_PATTERN_LEN; i++) {
    EEPROM.put(EEPROM_DATA_START + (MAX_PATTERN_LEN * sizeof(long)) + i, patternDelay[i]);
  }
}

void loadPattern() {
  byte meta;
  EEPROM.get(EEPROM_META_CELL, meta);
  
  if ((meta & META_PATTERN_SAVED) != META_PATTERN_SAVED) {
    return;
  }

  EEPROM.get(EEPROM_MODE_CELL, mode);
  debugPrint("loaded mode");
  debugPrintln(mode)

  for (int i = 0; i < MAX_PATTERN_LEN; i++) {
    // patternColours[i] = EEPROM.read(EEPROM_DATA_START + i);
    EEPROM.get(EEPROM_DATA_START + (i * sizeof(long)), patternColours[i]);
    debugPrint("loaded colour");
    debugPrintln(patternColours[i])
  }

  for (int i = 0; i < MAX_PATTERN_LEN; i++) {
    // patternDelay[i] = EEPROM.read(EEPROM_DATA_START + MAX_PATTERN_LEN + i);
    EEPROM.get(EEPROM_DATA_START + (MAX_PATTERN_LEN * sizeof(long)) + i, patternDelay[i]);
    debugPrint("loaded delay");
    debugPrintln(patternDelay[i])
  }

  Serial.println("Pattern Loaded");
  displayInitial();
}

void resetEeprom() {
  for (int i=0; i<EEPROM.length(); i++) {
    EEPROM.put(i, 0);
  }
  Serial.println("EEPROM Reset");
}

/* Other Input Methods */
void printHelp() {
  Serial.println("== LED CONTROLLER HELP ==");
  Serial.println("Input always starts with a \"key letter\" to specify the command.");
  Serial.println("Commands:");
  Serial.println("S - Display static colour (eg. \"S,FF000\")");
  Serial.println("J - Display jump pattern (see documentation)");
  Serial.println("F - Display fade pattern (see documentation)");
  Serial.println("D - Show a demo pattern (eg. \"D0\"). See docs for list");
  Serial.println("C - Commit (save) the current pattern for next power on");
  Serial.println("R - Reset (clear) the saved pattern");
  Serial.println("H - Show this help info");
  Serial.println("=========================");
}


/* Timer Methods */

// Setup Timer 1 to use for interrupts, which will be used to update the light patterns.
// Note: Timer 1 is used instead of Timer 0, as 0 is used for the millis() function
void setupTimer() {  
  // Reset Timer 1 Control Registers
  TCCR1A = 0;
  TCCR1B = 0;

  // Reset Timer Counter to 0
  TCNT1 = 0;

  // Set Timer pre-scaler to 1024. This gives an tick every 64us.
  // IMPORTANT: Change the math constants at the top if this gets changed!
  TCCR1B |= B00000101;

  // Re-enable interrupts
  sei();
}

void setTimer(unsigned int intervalMs) {
  double ticks = intervalMs * ((double) TIMER_TICKS_PER_MS);
  debugPrint("Set timer, requested ticks: ");
  debugPrintln(ticks);

  // Check if we overflow the 16-bit register, and if so then we need to handle these
  // longer values in a different way
  if (ticks > TIMER_TICKS_MAX) {
    debugPrintln("Ticks greater than max");
    useTimerCount = true;
    timerCount = 0;
    ticks = calculateTicksAndSetTarget(ticks);
  } else {
    useTimerCount = false;
  }

  ticks = round(ticks);

  debugPrint("Final ticks: ");
  debugPrintln(ticks);
  debugPrint("Timer target: ");
  debugPrintln(timerTarget);

  // Set the tick value at which we will interrupt
  OCR1A = (int) ticks;

  // Enable comparison interrupts for OCR1A
  TIMSK1 |= B00000010;
}

unsigned long calculateTicksAndSetTarget(unsigned long requestedTicks) {
  unsigned long ticks;
  long quotient = (long) ceil((double)requestedTicks / TIMER_TICKS_MAX);
  long remainder = requestedTicks % TIMER_TICKS_MAX;
  if (remainder == 0) {
    debugPrintln("rem of 0");
    ticks = TIMER_TICKS_MAX;
    timerTarget = quotient;
  } else {
    if ((remainder % quotient) <= REMAINDER_FUDGE_TICKS) {
      debugPrintln("q rem of 0");
      ticks = round(((quotient - 1) * TIMER_TICKS_MAX + remainder)/quotient);
      timerTarget = quotient;
    } else {
      debugPrintln("garbage");
      ticks = calculateTicksAndSetTarget(requestedTicks - 1);
    }
  }
  
  return ticks;
}

void resetTimer() {
  resetTimer(false);
}

void resetTimer(bool disableInterrupt) {
  debugPrintln("RESET TIMER");
  TCNT1 = 0; // Reset the tick count
  if (disableInterrupt || !useTimerCount || (useTimerCount && timerCount >= timerTarget)) {
    TIMSK1 &= B11111101; // Disable interrupts for the timer, will be re-enabled when the timer is set again
  }
}

void enableTimerInterrupts() {
  TIMSK1 |= B00000010;
}

void disableTimerInterrupts() {
  TIMSK1 &= B11111101;
}

/* Interrupt Handlers */

// Timer 1 Interrupt Service Routine
ISR(TIMER1_COMPA_vect){
  debugPrintln("ISR");
  if (useTimerCount)
    timerCount ++;
  resetTimer();
  if (!useTimerCount || timerCount >= timerTarget)
    displayNext();
}
