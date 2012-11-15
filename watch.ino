/*
 6-13-2011
 Low Power Testing
 Spark Fun Electronics 2011
 Nathan Seidle
 
 This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
 
 This code is written to control a 7 segment display with as little power as possible, waking up only 
 when the external button is hit or when the 32kHz oscillator rolls over every 8 seconds.

 Make sure to turn off Brown out detect (uses ~16uA).
 
 6-23-2011: Down to 38uA!
 
 6-29-2011: 17uA, not sure why.
 
 7-2-2011: Currently (hah!) at 1.13uA
 
 7-4-2011: Let's wake up every 8 seconds instead of every 1 to save even more power
 Since we don't display seconds, this should be fine
 Let's reduce the system clock to 8MHz/256 so that when servicing Timer2 int, we run even lower
 We are now at ~1.05uA on average
 
 7-17-2011: 1.09uA, portable with coincell power
 Jumps to 1.47uA every 8 seconds with system clock lowered to 1MHz
 Jumps to 1.37uA every 8 seconds with system clock at 8MHz
 Let's keep the internal clock at 8MHz since it doesn't seem to help to lower the internal clock.
 
 */
#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h> //Needed for powering down perihperals such as the ADC/TWI and Timers
#include "ePaper.h"

//For testing
int seconds = 0;
int minutes = 32;
int hours = 3;
int pm = true;
int update_the_time = false;
int button_pressed = false;

int EIOpin = 15;     // Input/output pin for chip selection
int XCKpin = 16;     // Clock input pin for taking display data
int LATCHpin = 17;   // Latch pulse input pin for display data
int SLEEPBpin = 18;  // Sleep Pin for the display
int DI0pin = 19;     // Input pin for display data
int ENpin = 5;       // Enable pin for boost regulator
int VCCpin = 6;      // Vcc of the display

int theButton = 2;

ePaper epaper = ePaper(EIOpin, XCKpin, LATCHpin, SLEEPBpin, DI0pin, ENpin, VCCpin);

//The very important 32.686kHz interrupt handler
ISR(TIMER2_OVF_vect){
  seconds += 8; //We sleep for 8 seconds instead of 1 to save more power
  //seconds++; //Use this if we are waking up every second
  updateTime();
}

//The interrupt occurs when you push the button
ISR(INT0_vect){
  //When you hit the button, we will need to display the time
  update_the_time = true;
  button_pressed = true;
}

void setup() {                
  //To reduce power, setup all pins as inputs with no pullups
  //for(int x = 1 ; x < 18 ; x++){
  //  pinMode(x, INPUT);
  //  digitalWrite(x, LOW);
  //}

  pinMode(theButton, INPUT); //This is the main button, tied to INT0
  digitalWrite(theButton, HIGH); //Enable internal pull up on button

  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();

  //Shut off ADC, TWI, SPI, Timer0, Timer1

  ADCSRA &= ~(1<<ADEN); //Disable ADC
  ACSR = (1<<ACD); //Disable the analog comparator
  DIDR0 = 0x3F; //Disable digital input buffers on all ADC0-ADC5 pins
  DIDR1 = (1<<AIN1D)|(1<<AIN0D); //Disable digital input buffer on AIN1/0
  
  power_twi_disable();
  power_spi_disable();
  //power_usart0_disable();
  //power_timer0_disable(); //Needed for delay_ms
  power_timer1_disable();
  //power_timer2_disable(); //Needed for asynchronous 32kHz operation

  //Setup TIMER2
  TCCR2A = 0x00;
  //TCCR2B = (1<<CS22)|(1<<CS20); //Set CLK/128 or overflow interrupt every 1s
  TCCR2B = (1<<CS22)|(1<<CS21)|(1<<CS20); //Set CLK/1024 or overflow interrupt every 8s
  ASSR = (1<<AS2); //Enable asynchronous operation
  TIMSK2 = (1<<TOIE2); //Enable the timer 2 interrupt

  //Setup external INT0 interrupt
  EICRA = (1<<ISC01); //Interrupt on falling edge
  EIMSK = (1<<INT0); //Enable INT0 interrupt

  //System clock futzing
  //CLKPR = (1<<CLKPCE); //Enable clock writing
  //CLKPR = (1<<CLKPS3); //Divid the system clock by 256

  Serial.begin(9600);  
  Serial.println("32kHz Testing:");
  delay(100);

  sei(); //Enable global interrupts
  //pinMode(13, OUTPUT);
}

void loop() {
  
  sleep_mode(); //Stop everything and go to sleep. Wake up if the Timer2 buffer overflows or if you hit the button

  if(update_the_time == true) {

    Serial.print(hours, DEC);
    Serial.print(":");
    Serial.print(minutes, DEC);
    Serial.print(":");
    Serial.println(seconds, DEC);

    showTime(); //Show the current time

    //If you are STILL holding the button, then you must want to adjust the time
    if(button_pressed && digitalRead(theButton) == LOW) setTime();
    
    Serial.println("not updating time");
    delay(100);

    update_the_time = false; //Reset the button variable
    button_pressed = false;
  }
  //Serial.println("8 seconds passed");
}

void updateTime() {
  //Update the minutes and hours variables
  update_the_time = seconds / 60;
  minutes += update_the_time; //Example: seconds = 2317, minutes = 58 + 38 = 96
  seconds %= 60; //seconds = 37
  hours += minutes / 60; //12 + (96 / 60) = 13
  minutes %= 60; //minutes = 36

  //In 12 hour mode, hours go from 12 to 1 to 12.
  while(hours > 12) {
    hours -= 12;
    pm = !pm;
  }
}

void showTime() {
  char strtime[11];
  const char *meridian = pm ? "pm" : "am";
  snprintf(strtime, 11, "%02d %02d %s", hours, minutes, meridian);
  //Serial.println(strtime);
  epaper.writeTop(strtime);
  epaper.writeNumberBottom(seconds);
  epaper.writeDisplay();
}

//This routine occurs when you hold the button down
//The colon blinks indicating we are in this mode
//Holding the button down will increase the time (accelerates)
//Releasing the button for more than 2 seconds will exit this mode
void setTime(void) {
  Serial.println("started updating time");
  delay(100);
  cli(); //We don't want the interrupt changing values at the same time we are!

  while(digitalRead(theButton) == LOW) {
    Serial.println("updating time");
    delay(100);

    //Update the minutes and hours variables
    updateTime();

    showTime();

    //Start advancing on the tens digit. Floor the single minute digit.
    minutes /= 10; //minutes = 46 / 10 = 4
    minutes *= 10; //minutes = 4 * 10 = 40
    minutes += 10;  //minutes = 40 + 10 = 50
  }
  
  sei(); //Resume interrupts
}
