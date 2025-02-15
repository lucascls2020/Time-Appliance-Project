#include "DPLL_AD9546.h"
#include <Arduino.h>



// DPLL Pins 
extern const uint8_t PIN_DPLL_SS;
extern const uint8_t PIN_DPLL_SCK;
extern const uint8_t PIN_DPLL_DAT;
extern const uint8_t PIN_DPLL_RESETB;

extern const PROGMEM uint16_t dpll_regs[];
extern const PROGMEM uint8_t dpll_vals[];


bool debug_dpll_print = true;

/* 3-pin SPI mode by default
 *  SCK is low by default
 *  CSB is active low
 *  Data is transferred out on rising edge
 *  Data is transferred in on falling edge
 */


void dpll_stop() { digitalWrite(PIN_DPLL_SS,1); }
void dpll_start() { digitalWrite(PIN_DPLL_SS,0); }





void convert_to_40bit(int64_t value, byte data[5]) {
  memset(data, 0, 5);
  if ( value < 0 ) {
    // need to do two's complement
    int64_t unsignedval = ( value * -1 ); // get the value
    unsignedval &= 0x7fffffffff; // look at the signed 40-bit value only
    unsignedval = (~unsignedval) + 1; // convert to two's complement
    memcpy(data, &unsignedval, 5); // copy the 5 bytes   
  } else {
    // same value, just copy it over
    memcpy(data, &value, 5);
  }
}



void dpll_write_register(int addr, byte data) {
  // this is more straight forward, basically shift out addr and data on rising edges
  // SCK is low idle but make sure it is
  dpll_start();
  pinMode(PIN_DPLL_DAT, OUTPUT);
  digitalWrite(PIN_DPLL_SCK, 0);

  for ( int i = 15; i >= 0; i-- ) {
    digitalWrite(PIN_DPLL_DAT, (bool)((addr & (1 << i)) >> i ) ); 
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 1);
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 0);
  }

  for ( int i = 7; i >= 0; i-- ) {
    digitalWrite(PIN_DPLL_DAT, (bool)((data & (1 << i)) >> i ) ); 
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 1);
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 0);
  }
  delayMicroseconds(1);
  dpll_stop();
  delayMicroseconds(1);
  if ( debug_dpll_print ) {
    Serial.print("dpll_write_register addr 0x"); Serial.print(addr, HEX);
    Serial.print(" data 0x"); Serial.println(data, HEX);
  }
}

byte dpll_read_register(int addr) {

  // EVB does a write to 0xf with 0x1, basically IO update, before doing arbitrary read
  // do the same I guess
  //dpll_write_register(0xf, 0x1);

  
  
  byte to_return = 0;
  dpll_start();
  // bit 15 of register address is R / !W bit, set it high
  addr |= (1<<15);

  pinMode(PIN_DPLL_DAT, OUTPUT);
  // Send first eight bytes 
  // data on rising edge
  // SCK is low for idle
  
  to_return = (byte) ((addr & 0xff00) >> 8);
  for ( int i = 7; i >= 0; i-- ) {
    digitalWrite(PIN_DPLL_DAT, (bool)((to_return & (1 << i)) >> i ) ); 
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 1);
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 0);
  }
  delayMicroseconds(1);

  to_return = (byte) (addr & 0xff);
  // Send next eight bytes
  // Data on rising edge but don't trigger falling edge on last
  for ( int i = 7; i >= 1; i-- ) {
    digitalWrite(PIN_DPLL_DAT, (bool)((to_return & (1 << i)) >> i ) ); 
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 1);
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 0);
  }

  // SCK is low, send out last bit on rising edge
  digitalWrite(PIN_DPLL_DAT, (bool)(to_return & 0x1));
  delayMicroseconds(1);
  digitalWrite(PIN_DPLL_SCK, 1);

  // now need to read input on falling edges
  pinMode(PIN_DPLL_DAT, INPUT);
  to_return = 0;
  delayMicroseconds(1);

  for ( int i = 0; i < 8; i++ ) {
    digitalWrite(PIN_DPLL_SCK, 0);
    to_return <<= 1;
    to_return += (byte) digitalRead(PIN_DPLL_DAT);
    delayMicroseconds(1);
    digitalWrite(PIN_DPLL_SCK, 1);
    delayMicroseconds(1);
  }


  
  dpll_stop();
  delayMicroseconds(1);
  // now put SCK back to idle and end
  digitalWrite(PIN_DPLL_SCK, 0);

  if ( debug_dpll_print ) {
    Serial.print("DPLL Read register 0x"); Serial.print(addr & 0x7fff, HEX);
    Serial.print(" = 0x"); Serial.println(to_return, HEX);
  }
  return to_return;
}


void dpll_init(const PROGMEM uint16_t regcount, const PROGMEM uint16_t regs[],const PROGMEM uint8_t vals[]) {

  pinMode(PIN_DPLL_SS, OUTPUT);
  pinMode(PIN_DPLL_SCK, OUTPUT);
  pinMode(PIN_DPLL_DAT, OUTPUT);
  pinMode(PIN_DPLL_RESETB, OUTPUT);
  digitalWrite(PIN_DPLL_SCK, 0);
  digitalWrite(PIN_DPLL_DAT, 0);
  digitalWrite(PIN_DPLL_SS, 1);

  // toggle the reset pin
  digitalWrite(PIN_DPLL_RESETB, 0); // make sure reset is high
  delay(50);
  digitalWrite(PIN_DPLL_RESETB, 1); // make sure reset is high


  // do a sanity check, read the ID register
  byte rddata = 0;
  while (1) {
    rddata = dpll_read_register(DEVICE_CODE_0);
    Serial.print("DPLL Debug read Register 0x"); Serial.print(rddata, HEX);
    Serial.print(" = 0x"); Serial.println(rddata, HEX);
    if ( rddata == 0x21 ) break;
    Serial.print("WAITING FOR 0x21");
    delay(1000);
  }
  Serial.println("Writing DPLL config from file");
  
  /* Write the config from the file */
  for ( int i = 0; i < regcount; i++ ) {
    dpll_write_register( regs[i], vals[i] );
  }

  /*
  // JULIAN HACK, make sure 0x280f[1] = 1
  Serial.println("JULIAN HACK MAKE SURE 0x280f[1] = 1");
  dpll_write_register( 0x280f, dpll_read_register(0x280f) | 0x2 );

  */
  Serial.println("JULIAN HACK MAKE SURE 0x111a[4] = 0");
  dpll_write_register( 0x111a, dpll_read_register(0x111a) & 0xEF );

  Serial.println("JULIAN HACK MAKE SURE 0x111a[3] = 1");
  dpll_write_register( 0x111a, dpll_read_register(0x111a) | (1<<3) );

  Serial.println("JULIAN HACK MAKE SURE 0x111a[2:0] = 6");
  dpll_write_register( 0x111a, (dpll_read_register(0x111a) & 0x7) + 6 );
  
  
  // Calibrate all VCOs by setting bit 1 to 1 in register 0x2000
  rddata = dpll_read_register(0x2000);
  dpll_write_register(0x2000, rddata | 0x2);
  dpll_io_update();
  delay(2000); // wait 2 seconds for the PLLs to calibrate 
  dpll_write_register(0x2000, rddata);
  dpll_io_update();

  // Synchronize all distribution dividers by setting bit 3 in register 0x2000 to 1
  rddata = dpll_read_register(0x2000);
  dpll_write_register(0x2000, rddata | 0x8);
  dpll_io_update();
  dpll_write_register(0x2000, rddata);
  dpll_io_update();
  
}



void dpll_io_update() {
  dpll_write_register(0xf, 0x1);
}

// To adjust outputs, adjusting AuxNCO0 frequency and phase
// for frequency, adjust the center frequency, in steps of 2^-40 Hz
// Is there a disadvantage to center frequency vs offset frequency?

// For phase adjust, adjust delta T, which is in picoseconds

// Following servo similar to ptp4l or ts2phc etc.




void dpll_set_phase(int64_t picoseconds) {
  // THIS FUNCTION DOES NOT ALLOW LARGE JUMPS, and DPLL tracking takes times
  // better to use distribution offsets
  // for purposes of aligning the PHC inside the DPLL
  // need to rely on looping back the 1PPS to the DPLL itself 
  // register AUXNCO0_PHASEOFFSET , AUXNCO0_PHASEOFFSET_SIZE
  // basically just write picoseconds to the register

  dpll_read_register( 0x280f );
  dpll_read_register( 0x2810 );
  dpll_read_register( 0x2811 );
  dpll_read_register( 0x2812 );
  dpll_read_register( 0x2813 );

  byte data[5];

  convert_to_40bit(picoseconds, data);
  
  for ( int i = 0; i < AUXNCO0_PHASEOFFSET_SIZE; i++ ) {
    dpll_write_register( AUXNCO0_PHASEOFFSET + i , data[i] ); 
  }
  dpll_io_update();
}






// adjFreq in units of 2^-40 Hz
void dpll_adjust_frequency(uint64_t adjFreq) {
  // register AUXNCO0_CENTERFREQ , AUXNCO0_CENTERFREQ_SIZE
  for ( int i = 0; i < AUXNCO0_CENTERFREQ_SIZE; i++ ) {
    dpll_write_register(  AUXNCO0_CENTERFREQ + i ,
      (adjFreq >> (8*i)) & 0xff );
  }
  dpll_io_update();
}


// Top level function called outside this DPLL library
// takes the error from the ptp protocol in picoseconds
// and adjusts the DPLL offset only for now 

extern int64_t picosecond_offset; // absolute offset, remote time - local time of a shared event
extern double frequency_ratio; // ratio of (remote frequency / local frequency)
extern bool update_dpll;  // PTP will return offset and frequency to adjust, 


void dpll_discipline_offset() {

  // The DPLL distribution outputs are phase based
  // I can set the output phase from 0 to 360 degree with finite resolution
  // I need to determine what phase value to write to the DPLL based on offset I find

  // Based on current distribution phase offset, need to adjust it based on picosecond_offset
  // 0 to 360 degrees = 0 to 1 seconds based on the offset being from 1PPS
  // But picosecond_offset can be negative, in -1 to 1 second range in theory
  // a small negative value in picosecond_offset is almost a max value in DPLL phase, close to 360


  Serial.print("dpll_discipline_offset "); Serial.println(picosecond_offset);
  // first read back the divide ratio
  int64_t divratio = 0;
  int64_t phasevalue = 0;
  for ( int i = 0; i < 4; i++ ) {
    divratio += ((int64_t)dpll_read_register(0x1112 + i)) << (8*i);    
  }
  for ( int i = 0; i < 4; i++ ) {
    phasevalue += ((int64_t)dpll_read_register(0x1116 + i)) << (8*i);    
  }  
  if ( dpll_read_register(0x111a) & (1<<6) ) {
    phasevalue += ((int64_t)1)<<32; // bit 32 is 0x111a[6]
  }
  
  Serial.print("Divratio:0x"); Serial.println(divratio, HEX);
  Serial.print("Initial phasevalue:0x"); Serial.println(phasevalue,HEX);

  // convert picosecond_offset to degrees 
  // period is 1Hz , 360 degrees in 1e12 picoseconds, multiply by that ratio 
  double offset_degrees = 0;
  offset_degrees = (360 / 1e12) * ((double)picosecond_offset); 
  if ( offset_degrees < 0.0 ) { // if it's negative, shift it into 0 to 360 range 
    offset_degrees += 360.0;
  }
  Serial.print("Offset degrees:"); Serial.println(offset_degrees,5);

  // convert current phase to degrees
  // 180 degrees per divratio units, multiply current value by that ratio 
  double current_degrees = 0;
  current_degrees = (((double) 180.0 ) / ((double) divratio)) * ((double)phasevalue);
  Serial.print("Current degrees:"); Serial.println(current_degrees,5);

  current_degrees += offset_degrees;
  if ( current_degrees >= 360.0 ) { // if it's outside the range, shift it back into range
    current_degrees -= 360.0; 
  }
  Serial.print("Current degrees after offset:"); Serial.println(current_degrees,5);

  // convert degrees into register value
  phasevalue = (int64_t) ( ( ((double)divratio) / ((double) 180.0) ) * current_degrees ); 
  
  Serial.print("####################Phasevalue:0x"); Serial.println(phasevalue,HEX);
  
  // write lower four bytes to register
  for ( int i = 0; i < 4; i++ ) {
    dpll_write_register( 0x1116 + i, (phasevalue >> (8*i)) & 0xff );
  }
  // set bit 33 in register 0x111a
  if ( phasevalue & (0x100000000) ) {
    dpll_write_register( 0x111a, dpll_read_register(0x111a) | (1<<6));
  } else {
    dpll_write_register( 0x111a, dpll_read_register(0x111a) & ~(1<<6));
  }

  dpll_io_update();
}

void dpll_discipline_freq() {
  // frequency need to read back and do the multiplication
  double centerfreq = 0;
  uint64_t int_center = 0;
  for ( int i = 0; i < AUXNCO0_CENTERFREQ_SIZE; i++ ) {
    centerfreq += (double)(((uint64_t) dpll_read_register( AUXNCO0_CENTERFREQ + i )) << (8*i));
  }
  Serial.print("dpll_discipline_freq read frequency value in units of 2^-40 Hz: "); Serial.println(centerfreq,HEX);
  centerfreq *= frequency_ratio;

  int_center = (uint64_t) centerfreq;
  

  Serial.print("######################dpll_discipline_freq change to 0x"); Serial.println(int_center, HEX);

  for ( int i = 0; i < AUXNCO0_CENTERFREQ_SIZE; i++ ) {
    dpll_write_register(  AUXNCO0_CENTERFREQ + i ,
      (int_center >> (8*i)) & 0xff );
  }

  dpll_io_update();
}

// DPLL needs time to adjust to large changes in frequency
// check this first 
bool is_dpll_still_adjusting() {
  return false;
  byte data = 0;
  data = dpll_read_register(PLL0_STATUS_0);  
}


//int64_t debug_picoseconds = MILLI_TO_PICO(-1);
bool dpll_adjust_error() {
  Serial.print("DPLL Adjust error:");
  Serial.print(picosecond_offset); Serial.print(" ");
  Serial.print(frequency_ratio, 12); Serial.println(" ");


  /*
  if ( debug_picoseconds == 0 ) {
    debug_picoseconds = MILLI_TO_PICO(-1);
  } else {
    debug_picoseconds = 0;
  }
  Serial.println("###################HACK##################"); dpll_set_dist_phase(debug_picoseconds); return;
  */

  
  if (is_dpll_still_adjusting() ) {
    return false; // still doing something from previous loop, just back out
  }

  Serial.print("FREQ_PPB:"); Serial.println(FREQ_PPB(frequency_ratio));

  if ( (FREQ_PPM(frequency_ratio) >= 1000) || (FREQ_PPM(frequency_ratio) <= -1000) ) { // sanity check, 100ppm is probably math error somewhere or protocol error
    return false;
  }
  // simple algorithm
  if ( (FREQ_PPB(frequency_ratio) > 100) || (FREQ_PPB(frequency_ratio) < -100) ) { 
    // adjust frequency first
    dpll_discipline_freq();
    dpll_discipline_offset();
    return true;
  } else if ( (picosecond_offset > MICRO_TO_PICO(1)) || (picosecond_offset < MICRO_TO_PICO(-1) ) )  {
    dpll_discipline_offset();
    return true;
  }  
  return false;
}
