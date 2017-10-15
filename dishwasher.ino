// Early alpha, still work in progress!

#include <EEPROM.h>            // Used to store current stage of wash in case of power loss and then restart it from where it stalled
#include <Timer.h>             // For real-time operations (no 'delay'-s, please!)
#define  DEBUG                 // Comment out this line for "production" mode, if you need to reduce SRAM memory usage
//#include "DebugUtils.h"        // Debug library based on http://forum.arduino.cc/index.php?topic=46900.0
//#include <avr/wdt.h>

//#define EEPROM_shift    0  // Arduino's memory has a limited lifetime - datasheet claims it it limited to 10000 writes (although some tests show it is capable to handle millions of writes). To be on the safe side I plan to move the location of my state variables on a yearly basis - I will change it to 50, than to 100, etc. (Remember - Arduino Leonardo's EEPROM is 1024 bytees only!)

#define float2long(x)      ((x)>=0?(long)((x)+0.5):(long)((x)-0.5)) // Function later used to display temperatures

#define pin_Salt       13  // Solenoid valve that makes water flow through special chamber with dishwasher salt, which reduces its pH level
#define pin_MotorPump  12  // Motor pump (this one actually washes dishes by pumping water into carousel on the bottom of water tank).
#define pin_Inlet      11  // Water inlet valve
#define pin_Drain       10  // The pump that flushes dirty water to the drain
#define pin_Cleanser    9  // Releases the lock that holds the cleanser tablet or detergent powder
#define //pin_RinseLiquid 8  // My dishwasher uses single electromagnetic valve to release both detergent and rinse liquid, hence same pin for both
#define pin_Heater      8  // Heating element. (Note: mine has built-in safety thermostats that cut the line if temperature exceeds 75C)
#define pin_Pressostat  7  // Pressostat (conducts current when water is in tank; doesn't conduct if tank is empty)
#define pin_Reset       6  // Connected to RSET pin of Arduino. See http://www.instructables.com/id/two-ways-to-reset-arduino-in-software/?ALLSTEPS
#define pin_Door        5  // Safety switch on the door. When it releases, we must immediately stop the main motor pump!
#define pin_Buzzer     4  // Buzzer. Notice that pins 14-16 are available only on Leonardo and MEGA, you don't get these on Uno.
#define pin_ButtonP    3  // Program selection button
//#define pin_ButtonE    16  // "Eco" button. Don't forget to add two pull-down resistors (10 KOhm) between pins 11 & 12 and GND. See http://arduino.cc/en/Tutorial/Button for details.
#define pin_Thermo A0  // My dishwasher had ntc thermistor 10kom.
//#define pin_LEDE       13  // This LED corresponds to "Eco" button on my dishwasher. Arduino has a bad habit of blinking its built-in LED on pin 13 extensively at startup. I don't want some relay clicking and water flowing just because of this, so I've connected the most harmless peripheral to it.
#define pin_LED1       A1  // LEDs 1 to 5 are just regular ones - original dishwasher used them to select one of 5 preinstalled programs.
#define pin_LED2       A2
#define pin_LED3       A3
#define pin_LED4       A4  // Usually, pins A4 and A5 are used for i2c bus. But Arduino Leonardo that I am using for my project uses digital, not analog pins for  SDA and SCL.
#define pin_LED5       A5



// Few more things for buttons. They have a bad habit of 'bouncing', when accidental flickers can be mistekenly interpreted by Arduino as actual key presses. We will need to make sure key is pressed for at least 10ms, or else we'll ignore it.
const int debounceDelay = 4;  // milliseconds to wait until stable

// These variables will be acting as semaphores to store previous readings of inputs:
bool flag_pin_Pressostat  = false;
bool flag_pin_Door        = false;
bool flag_pin_ButtonP     = false;
bool flag_pin_ButtonE     = false;

// ...and status of outputs:
bool flag_pin_Inlet       = false;
bool flag_pin_Drain       = false;
bool flag_pin_Cleanser    = false;
bool flag_pin_MotorPump   = false;
bool flag_pin_Heater      = false;
bool flag_pin_RinseLiquid = false;
bool flag_pin_Salt        = false;

bool flag_pin_LEDE = false;
bool flag_pin_LED1 = false;
bool flag_pin_LED2 = false;
bool flag_pin_LED3 = false;
bool flag_pin_LED4 = false;
bool flag_pin_LED5 = false;

// TBD timers that hold the last time relay was flipped, to prevent excessive use

void setup() {
  digitalWrite(pin_Reset,  HIGH); // To avoid getting trapped into reset loop, immediately after start we set RSET pin to HIGH. Setting it to LOW in the stopOnError() function will reset the board.

  pinMode(pin_LEDE,        OUTPUT);
  pinMode(pin_LED1,        OUTPUT);
  pinMode(pin_LED2,        OUTPUT);
  pinMode(pin_LED3,        OUTPUT);
  pinMode(pin_LED4,        OUTPUT);
  pinMode(pin_LED5,        OUTPUT);

  pinMode(pin_Reset,       OUTPUT);

  pinMode(pin_Cleanser,    OUTPUT);
  pinMode(pin_RinseLiquid, OUTPUT);
  pinMode(pin_Heater,      OUTPUT);
  pinMode(pin_Salt,        OUTPUT);
  pinMode(pin_Drain,       OUTPUT);
  pinMode(pin_MotorPump,   OUTPUT);
  pinMode(pin_Buzzer,      OUTPUT);
  pinMode(pin_Inlet,       OUTPUT);

  pinMode(pin_Thermo,  INPUT);
  pinMode(pin_ButtonP,     INPUT); // My wiring has existing resistor,
  pinMode(pin_ButtonE,     INPUT); // so no need to use the one built into Arduino for pull-up.

  pinMode(pin_Pressostat,  INPUT_PULLUP); // "INPUT_PULLUP" is a nice trick that saves you from the need to _write_ HIGH to _output_ pin.
  pinMode(pin_Door,        INPUT_PULLUP);

  // Let's make sure everything is turned off initially, to be on the safe side!
  shutdownEverything();

  // And let's kill the lights, too:
  digitalWrite(pin_LED1,        LOW);
  digitalWrite(pin_LED2,        LOW);
  digitalWrite(pin_LED3,        LOW);
  digitalWrite(pin_LED4,        LOW);
  digitalWrite(pin_LED5,        LOW);
  digitalWrite(pin_LEDE,        LOW);

  buzz(3000, 6);

  /*
  Serial.begin(115200);
  #ifdef DEBUG
    #ifdef __AVR_ATmega32U4__
      while (!Serial) {
        // Required for Arduino Leonardo only: waits while serial connection is established. Comment this out for production configuration.
      }
    #endif
  #endif
  */

  Serial1.begin(115200); // If you are getting compiilation error at this line, you are trying to compile the sketch for a board other than Leonardo or MEGA...
  DEBUG_PRINTLN("Hi.");

  buzz(4000, 6);

  /*sensors.begin(); // Starts up the "DallasTemperature" library
  sensors.getDeviceCount();

  if (!sensors.getAddress(thermometerDoor,   0)) {
    DEBUG_PRINTLN(F("DS18B20 on Door not found"));
    stopOnError();
  }
  if (!sensors.getAddress(thermometerBottom, 1)) {
    DEBUG_PRINTLN(F("DS18B20 on Bottom not found"));
    stopOnError();
  }

  // We set the resolution of DS18B20 sensors to 9 bit:
  sensors.setResolution(thermometerDoor,   9);
  sensors.setResolution(thermometerBottom, 9);

  checkTempSensors();

  // Evaluating the initial state of sensors, writing them to semaphore variables:

  if (debounce(pin_Pressostat)) {
    // pin_Pressostat is down:
    flag_pin_Pressostat = false;
  } else {
    flag_pin_Pressostat = true;
  }

  if (debounce(pin_Door)) {
    // Door is open (non-operational mode!)
    flag_pin_Door = false;
  } else {
    // Door is closed (OK to proceed)
    flag_pin_Door = true;
  }
}

void checkTempSensors() {
  sensors.requestTemperatures(); // Issue a global temperature request to all devices on the bus
  // Under no curcumstances any of DS18B20's should report zero reading.
  // It is clearly a fault, so we must halt immediately before we blindly mess something up.
  if (float2long(sensors.getTempC(thermometerDoor)) == 0) {
    DEBUG_PRINTLN(F("DS18B20 on Door failed!"));
    stopOnError();
  }
  if (float2long(sensors.getTempC(thermometerBottom)) == 0) {
    DEBUG_PRINTLN(F("DS18B20 on Bottom failed!"));
    stopOnError();
  }
}
*/
  
void stopOnError() { // Halts execution of the sketch and keeps beeping to indicate that something went wrong.
  // Disables everything:
  shutdownEverything();
  int u = 0;
  while(u <= 5) {
    u++ ;
    buzz(4186, 12); // 4186 is a frequency in Hz for "C8" tone. See Pitches.h library on http://arduino.cc/en/Tutorial/Tone for details.
    delay(100);
    buzz(4186, 18);
    delay(400);
  }
  delay(10);
  digitalWrite(pin_Reset, LOW);
}

void shutdownEverything() { // Disables all peripherals in case of critical error
  digitalWrite(pin_Cleanser,    LOW);
  digitalWrite(pin_RinseLiquid, LOW);
  digitalWrite(pin_Heater,      LOW);
  digitalWrite(pin_Salt,        LOW);
  digitalWrite(pin_Drain,       LOW);
  digitalWrite(pin_MotorPump,   LOW);
  digitalWrite(pin_Buzzer,      LOW);
  digitalWrite(pin_Inlet,       LOW);
}

boolean debounce(int pin) { // Used to distinguish between phantom keypresses and real ones.
  boolean state;
  boolean previousState;
  previousState = digitalRead(pin); // We store switch state,
  for (int counter=0; counter < debounceDelay; counter++) {
    delay(1);                       // wait for 1 millisecond,
    state = digitalRead(pin);       // read the pin,
    if (state != previousState) {
      counter = 0;                  // reset the counter if the state changes,
      previousState = state;        // and save the current state,
    }
  }
  return state;                     // here when the switch state has been stable longer than the debounce period.
}

void buzz(long frequency, long length) { // Buzzer code borrowed from http://www.linuxcircle.com/2013/03/31/playing-mario-bros-tune-with-arduino-and-piezo-buzzer/
  long delayValue = 1000000 / frequency / 2;
  long numCycles = frequency * length / 1000;
  for (long i=0; i < numCycles; i++){
    digitalWrite(pin_Buzzer,HIGH);
    delayMicroseconds(delayValue);
    digitalWrite(pin_Buzzer,LOW);
    delayMicroseconds(delayValue);
  }
}

/*-void printTemperature(DeviceAddress deviceAddress) {
  float tempC = sensors.getTempC(deviceAddress);
  Serial1.println(float2long(tempC));
}
*/
// long printTemperature(DeviceAddress deviceAddress) { // Prints the temperature for a DS18B20 temperature sensor (in Celsius)
//  float tempC = sensors.getTempC(deviceAddress);
//  return float2long(tempC);
// }

void loop() {

//  checkTempSensors(); // We query the sensors for current temperatures

  if (debounce(pin_ButtonP)) {
    buzz(2349, 9);
    DEBUG_PRINTLN(F("pin_ButtonP"));
  }

  if (debounce(pin_ButtonE)) {
    buzz(2349, 9);
    digitalWrite(pin_LEDE, HIGH);
    DEBUG_PRINTLN(F("pin_ButtonE"));
  } else {
    digitalWrite(pin_LEDE, LOW);
  }

  if (debounce(pin_Pressostat)) {
    // We've detected that water pressure turned pin_Pressostat down:
    if (flag_pin_Pressostat == true) {
      // It was up in the previous moment, and flipped:
      DEBUG_PRINTLN(F("-pin_Pressostat"));
      flag_pin_Pressostat = false; // and we flip semaphore, too.
    } else {
      // It was the same in the previous moment, so no need to clutter the log.
    }
  } else {
    // We've detected that water pressure turned pin_Pressostat up:
    if (flag_pin_Pressostat == true) {
      // It was the same in the previous moment, so no need to clutter the log.
    } else {
      // It was up in the previous moment, and flipped:
      DEBUG_PRINTLN(F("+pin_Pressostat"));
      flag_pin_Pressostat = true; // and we flip semaphore, too.
    }
  }

  if (debounce(pin_Door)) {
    // We've detected that door is open (non-operational mode):
    if (flag_pin_Door == true) {
      // It was closed in the previous moment, and opened:
      DEBUG_PRINTLN(F("-pin_Door"));
      flag_pin_Door = false; // and we flip semaphore, too.
    } else {
      // It was the same in the previous moment, so no need to clutter the log.
    }
  } else {
    // We've detected that door was open and now it is closed:
    if (flag_pin_Door == true) {
      // It was the same in the previous moment, so no need to clutter the log.
    } else {
      // Door was open in the previous moment, and now it is closed (operational):
      DEBUG_PRINTLN(F("+pin_Door"));
      flag_pin_Door = true; // and we flip semaphore, too.
    }
  }

  manualKeyboardDebug();

}

void manualKeyboardDebug() { // Debug - here we test the operation of relays manually
  #ifdef DEBUG
  if (Serial1.available()) {
    char ch = Serial1.read();
    if ( isalnum(ch) ) {
      switch (ch) {
        case 'C': {
          DEBUG_PRINTLN(F("~'C'"));
          turnOn_pin_Cleanser();
          break;
        }
        case 'c': {
          DEBUG_PRINTLN(F("~'c'"));
          turnOff_pin_Cleanser();
          break;
        }
        case 'R': {
          DEBUG_PRINTLN(F("~'R'"));
          turnOn_pin_RinseLiquid();
          break;
        }
        case 'r': {
          DEBUG_PRINTLN(F("~'r'"));
          turnOff_pin_RinseLiquid();
          break;
        }
        case 'H': {
          DEBUG_PRINTLN(F("~'H'"));
          turnOn_pin_Heater();
          break;
        }
        case 'h': {
          DEBUG_PRINTLN(F("~'h'"));
          turnOff_pin_Heater();
          break;
        }
        case 'S': {
          DEBUG_PRINTLN(F("~'S'"));
          turnOn_pin_Salt();
          break;
        }
        case 's': {
          DEBUG_PRINTLN(F("~'s'"));
          turnOff_pin_Salt();
          break;
        }
        case 'D': {
          DEBUG_PRINTLN(F("~'D'"));
          turnOn_pin_Drain();
          break;
        }
        case 'd': {
          DEBUG_PRINTLN(F("~'d'"));
          turnOff_pin_Drain();
          break;
        }
        case 'M': {
          DEBUG_PRINTLN(F("~'M'"));
          turnOn_pin_MotorPump();
          break;
        }
        case 'm': {
          DEBUG_PRINTLN(F("~'m'"));
          turnOff_pin_MotorPump();
          break;
        }
        case 'I': {
          DEBUG_PRINTLN(F("~'I'"));
          turnOn_pin_Inlet();
          break;
        }
        case 'i': {
          DEBUG_PRINTLN(F("~'i'"));
          turnOff_pin_Inlet();
          break;
        }
        case 'E': {
          DEBUG_PRINTLN(F("~'E'"));
          turnOn_pin_LEDE();
          break;
        }
        case 'e': {
          DEBUG_PRINTLN(F("~'e'"));
          turnOff_pin_LEDE();
          break;
        }
        case '1': {
          DEBUG_PRINTLN(F("~'1'"));
          turnOn_pin_LED1();
          break;
        }
        case '6': {
          DEBUG_PRINTLN(F("~'6'"));
          turnOff_pin_LED1();
          break;
        }
        case '2': {
          DEBUG_PRINTLN(F("~'2'"));
          turnOn_pin_LED2();
          break;
        }
        case '7': {
          DEBUG_PRINTLN(F("~'7'"));
          turnOff_pin_LED2();
          break;
        }
        case '3': {
          DEBUG_PRINTLN(F("~'3'"));
          turnOn_pin_LED3();
          break;
        }
        case '8': {
          DEBUG_PRINTLN(F("~'8'"));
          turnOff_pin_LED3();
          break;
        }
        case '4': {
          DEBUG_PRINTLN(F("~'4'"));
          turnOn_pin_LED4();
          break;
        }
        case '9': {
          DEBUG_PRINTLN(F("~'9'"));
          turnOff_pin_LED4();
          break;
        }
        case '5': {
          DEBUG_PRINTLN(F("~'5'"));
          turnOn_pin_LED5();
          break;
        }
        case '0': {
          DEBUG_PRINTLN(F("~'0'"));
          turnOff_pin_LED5();
          break;
        }
        case 'X': {
          turnOn_pin_RSET();
          break;
        }
        default: {
          DEBUG_PRINTLN(F("Not recognized"));
          sensors.requestTemperatures();
          DEBUG_PRINT(F("Door t.: "));
          printTemperature(thermometerDoor);
          DEBUG_PRINT(F("Bottom t.: "));
          printTemperature(thermometerBottom);
          buzz(2500, 9);
          break;
        }
      }
      } else {
      DEBUG_PRINTLN(F("Only alphanum.input after '!'"));
    }
  }
  delay(1);
  #endif
}

void turnOn_pin_Cleanser() {
  DEBUG_PRINTLN(F("+'pin_Cleanser'"));
  digitalWrite(pin_Cleanser, HIGH);
  flag_pin_Cleanser = true;
}

void turnOff_pin_Cleanser() {
  DEBUG_PRINTLN(F("-'pin_Cleanser'"));
  digitalWrite(pin_Cleanser, LOW);
  flag_pin_Cleanser = false;
}

void turnOn_pin_RinseLiquid() {
  DEBUG_PRINTLN(F("+'pin_RinseLiquid'"));
  digitalWrite(pin_RinseLiquid, HIGH);
  flag_pin_RinseLiquid = true;
}

void turnOff_pin_RinseLiquid() {
  DEBUG_PRINTLN(F("-'pin_RinseLiquid'"));
  digitalWrite(pin_RinseLiquid, LOW);
  flag_pin_RinseLiquid = false;
}

void turnOn_pin_Heater()  {
  DEBUG_PRINTLN(F("+'pin_Heater'"));
  digitalWrite(pin_Heater, HIGH);
  flag_pin_Heater = true;
}

void turnOff_pin_Heater()  {
  DEBUG_PRINTLN(F("-'pin_Heater'"));
  digitalWrite(pin_Heater, LOW);
  flag_pin_Heater = false;
}

void turnOn_pin_Salt() {
  DEBUG_PRINTLN(F("+'pin_Salt'"));
  digitalWrite(pin_Salt, HIGH);
  flag_pin_Salt = true;
}

void turnOff_pin_Salt()  {
  DEBUG_PRINTLN(F("-'pin_Salt'"));
  digitalWrite(pin_Salt, LOW);
  flag_pin_Salt = false;
}

void turnOn_pin_Drain()  {
  DEBUG_PRINTLN(F("+'pin_Drain'"));
  digitalWrite(pin_Drain, HIGH);
  flag_pin_Drain = true;
}

void turnOff_pin_Drain()  {
  DEBUG_PRINTLN(F("-'pin_Drain'"));
  digitalWrite(pin_Drain, LOW);
  flag_pin_Drain = false;
}

void turnOn_pin_MotorPump()  {
  DEBUG_PRINTLN(F("+'pin_MotorPump'"));
  digitalWrite(pin_MotorPump, HIGH);
  flag_pin_MotorPump = true;
}

void turnOff_pin_MotorPump() {
  DEBUG_PRINTLN(F("-'pin_MotorPump'"));
  digitalWrite(pin_MotorPump, LOW);
  flag_pin_MotorPump = false;
}

void turnOn_pin_Inlet()  {
  DEBUG_PRINTLN(F("+'pin_Inlet'"));
  digitalWrite(pin_Inlet, HIGH);
  flag_pin_Inlet = true;
}

void turnOff_pin_Inlet()  {
  DEBUG_PRINTLN(F("-'pin_Inlet'"));
  digitalWrite(pin_Inlet, LOW);
  flag_pin_Inlet = false;
}

void turnOn_pin_LEDE() {
  DEBUG_PRINTLN(F("+'pin_LEDE'"));
  digitalWrite(pin_LEDE, HIGH);
  flag_pin_LEDE = true;
}

void turnOff_pin_LEDE() {
  DEBUG_PRINTLN(F("-'pin_LEDE'"));
  digitalWrite(pin_LEDE, LOW);
  flag_pin_LEDE = false;
}

void turnOn_pin_LED1() {
  DEBUG_PRINTLN(F("+'pin_LED1'"));
  digitalWrite(pin_LED1, HIGH);
  flag_pin_LED1 = true;
}

void turnOff_pin_LED1() {
  DEBUG_PRINTLN(F("-'pin_LED1'"));
  digitalWrite(pin_LED1, LOW);
  flag_pin_LED1 = false;
}

void turnOn_pin_LED2() {
  DEBUG_PRINTLN(F("+'pin_LED2'"));
  digitalWrite(pin_LED2, HIGH);
  flag_pin_LED2 = true;
}

void turnOff_pin_LED2() {
  DEBUG_PRINTLN(F("-'pin_LED2'"));
  digitalWrite(pin_LED2, LOW);
  flag_pin_LED2 = false;
}

void turnOn_pin_LED3() {
  DEBUG_PRINTLN(F("+'pin_LED3'"));
  digitalWrite(pin_LED3, HIGH);
  flag_pin_LED3 = true;
}

void turnOff_pin_LED3() {
  DEBUG_PRINTLN(F("-'pin_LED3'"));
  digitalWrite(pin_LED3, LOW);
  flag_pin_LED3 = false;
}

void turnOn_pin_LED4() {
  DEBUG_PRINTLN(F("+'pin_LED4'"));
  digitalWrite(pin_LED4, HIGH);
  flag_pin_LED4 = true;
}

void turnOff_pin_LED4() {
  DEBUG_PRINTLN(F("-'pin_LED4'"));
  digitalWrite(pin_LED4, LOW);
  flag_pin_LED4 = false;
}

void turnOn_pin_LED5() {
  DEBUG_PRINTLN(F("+'pin_LED5'"));
  digitalWrite(pin_LED5, HIGH);
  flag_pin_LED5 = true;
}

void turnOff_pin_LED5() {
  DEBUG_PRINTLN(F("-'pin_LED5'"));
  digitalWrite(pin_LED5, LOW);
  flag_pin_LED5 = false;
}

void turnOn_pin_RSET() {
  DEBUG_PRINTLN(F("RST"));
  stopOnError();
}
