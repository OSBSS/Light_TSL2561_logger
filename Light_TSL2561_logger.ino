//****************************************************************

// OSBSS Light datalogger code (based on the TSL2561)
// Last edited on March 30, 2015

//****************************************************************

#include <EEPROM.h>
#include <DS3234lib3.h>
#include <PowerSaver.h>
#include <SdFat.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>

// Launch Variables   ******************************
long interval = 60;  // set logging interval in SECONDS, eg: set 300 seconds for an interval of 5 mins
int dayStart = 24, hourStart = 19, minStart = 10;    // define logger start time: day of the month, hour, minute
char filename[15] = "log.csv";    // Set filename Format: "12345678.123". Cannot be more than 8 characters in length, contain spaces or begin with a number

// Global objects and variables   ******************************
#define POWA 4    // pin 4 supplies power to microSD card breakout and SHT15 sensor
#define LED 7  // pin 7 controls LED
int SDcsPin = 9; // pin 9 is CS pin for MicroSD breakout
long lux;

PowerSaver chip;  	// declare object for PowerSaver class
DS3234 RTC;    // declare object for DS3234 class
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
SdFat sd; 		// declare object for SdFat class
SdFile file;		// declare object for SdFile class

// ISR ****************************************************************
ISR(PCINT0_vect)  // Interrupt Vector Routine to be executed when pin 8 receives an interrupt.
{
  //PORTB ^= (1<<PORTB1);
  asm("nop");
}

// setup ****************************************************************
void setup()
{
  Serial.begin(19200); // open serial at 19200 bps
  
  pinMode(POWA, OUTPUT);  // set output pins
  pinMode(LED, OUTPUT);
  
  digitalWrite(POWA, HIGH);    // turn on SD card
  delay(1);    // give some delay to ensure SD gets to full power
  
  configureTSL2561();// configure light sensor after powering on
  
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))  // initialize SD card on the SPI bus - very important
  {
    delay(10);
    SDcardError();
  }
  else
  {
    delay(10);
    file.open(filename, O_CREAT | O_APPEND | O_WRITE);  // open file in write mode and append data to the end of file
    delay(1);
    String time = RTC.timeStamp();    // get date and time from RTC
    file.println();
    file.print("Date/Time,Light Intensity (lux)");    // Print header to file
    file.println();
    PrintFileTimeStamp();
    file.close();    // close file - very important
                     // give some delay by blinking status LED to wait for the file to properly close
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
  }
  RTC.checkInterval(hourStart, minStart, interval); // Check if the logging interval is in secs, mins or hours
  RTC.alarm2set(dayStart, hourStart, minStart);  // Configure begin time
  RTC.alarmFlagClear();  // clear alarm flag
                          
  chip.sleepInterruptSetup();    // setup sleep function & pin change interrupts on the ATmega328p. Power-down mode is used here
}

// loop ****************************************************************
void loop()
{
  
  digitalWrite(POWA, LOW);  // turn off microSD card to save power
  delay(1);  // give some delay for SD card and RTC to be low before processor sleeps to avoid it being stuck
  
  chip.turnOffADC();    // turn off ADC to save power
  chip.turnOffSPI();  // turn off SPI bus to save power
  //chip.turnOffWDT();  // turn off WatchDog Timer to save power (does not work for Pro Mini - only works for Uno)
  chip.turnOffBOD();    // turn off Brown-out detection to save power
  
  chip.goodNight();    // put processor in extreme power down mode - GOODNIGHT!
                       // this function saves previous states of analog pins and sets them to LOW INPUTS
                       // average current draw on Mini Pro should now be around 0.195 mA (with both onboard LEDs taken out)
                       // Processor will only wake up with an interrupt generated from the RTC, which occurs every logging interval
  
  // code will resume from here once the processor wakes up =============== //
  chip.turnOnADC();    // enable ADC after processor wakes up
  chip.turnOnSPI();   // turn on SPI bus once the processor wakes up
  delay(1);    // important delay to ensure SPI bus is properly activated
  
  RTC.alarmFlagClear();    // clear alarm flag
  
  pinMode(POWA, OUTPUT); 
  digitalWrite(POWA, HIGH);  // turn on SD card power
  delay(1);    // give delay to let the SD card and SHT15 get full powa
  
  RTC.checkDST(); // check and account for Daylight Savings Time in US
  
  // get sensor values
  configureTSL2561(); // configure light sensor again after powering on
  sensors_event_t event;
  tsl.getEvent(&event);
  if (event.light)
    lux = event.light;
  else
    lux = -1;
  
  pinMode(SDcsPin, OUTPUT);
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))    // very important - reinitialize SD card on the SPI bus
  {
    delay(10);
    SDcardError();
  }
  else
  {
    delay(10);
    file.open(filename, O_WRITE | O_AT_END);  // open file in write mode
    delay(1);
    
    String time = RTC.timeStamp();    // get date and time from RTC
    SPCR = 0;  // reset SPI control register
    
    file.print(time);
    file.print(",");
    file.print(lux);
    file.println();
    PrintFileTimeStamp();
    file.close();    // close file - very important
                     // give some delay by blinking status LED to wait for the file to properly close
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
  }
  RTC.setNextAlarm();      //set next alarm before sleeping
  delay(1);
}

// configure light sensor
void configureTSL2561()
{
  tsl.begin(); 
  tsl.enableAutoRange(true);
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */
}

// file timestamps ****************************************************************
void PrintFileTimeStamp() // Print timestamps to data file. Format: year, month, day, hour, min, sec
{ 
  file.timestamp(T_WRITE, RTC.year, RTC.month, RTC.day, RTC.hour, RTC.minute, RTC.second);    // edit date modified
  file.timestamp(T_ACCESS, RTC.year, RTC.month, RTC.day, RTC.hour, RTC.minute, RTC.second);    // edit date accessed
}

// SD card Error response ****************************************************************
void SDcardError()
{
    for(int i=0;i<3;i++)   // blink LED 3 times to indicate SD card write error
    {
      digitalWrite(LED, HIGH);
      delay(50);
      digitalWrite(LED, LOW);
      delay(150);
    }
}

//****************************************************************
