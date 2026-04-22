// Sound.cpp
// Runs on MSPM0
// Sound assets in sounds/sounds.h
// Jeslyn Chang
// your data 
#include <stdint.h>
#include <ti/devices/msp/msp.h>
#include "Sound.h"
#include "sounds/sounds.h"
#include "../inc/DAC5.h"
#include "../inc/Timer.h"



void SysTick_IntArm(uint32_t period, uint32_t priority){
  SysTick->CTRL = 0; //clear enable bit (bit 0) to turn off systick during init
  SysTick->LOAD = period - 1; //set the load register to period - 1 (will count down til 0, then trigger interrupt.)
      //btw. time it takes to interrupt is 12.5ns * load amt 
  SysTick->VAL = 0; // write any value to clear the CTRL counter. will reload the value basically, stores the load value into VAL (the volatile counter).
  SCB->SHP[1] = (SCB->SHP[1]&(~0xC0000000))|(priority<<30); // friendly set our priority bits to 31-30
  SysTick->CTRL = 0x05; // bit 2= selecting the 80 MHz bus clock to decrement VAL...bit 1= enable interrupts. .. bit 0 = set enable so counter will run...
}

// initialize a 11kHz SysTick, however no sound should be started
// initialize any global variables
// Initialize the 5 bit DAC
const uint8_t *pointer = 0;
uint32_t arrayCount = 0;
uint32_t arrayIndex = 0;
void Sound_Init(void){
  SysTick_IntArm(80000000/11000, 0);
  //enable_irq
  DAC5_Init();
//
}
extern "C" void SysTick_Handler(void);

void SysTick_Handler(void){ // called at 11 kHz
  // output one value to DAC if a sound is active 
  if(arrayIndex >= arrayCount){
    SysTick->CTRL = 0x05;
  }
  else{
    DAC5_Out(pointer[arrayIndex]); 
    arrayIndex++;
  }
}

//******* Sound_Start ************
// This function does not output to the DAC. 
// Rather, it sets a pointer and counter, and then enables the SysTick interrupt.
// It starts the sound, and the SysTick ISR does the output
// feel free to change the parameters
// Sound should play once and stop
// Input: pt is a pointer to an array of DAC outputs
//        count is the length of the array
// Output: none
// special cases: as you wish to implement
void Sound_Start(const uint8_t *pt, uint32_t count){
  SysTick->CTRL = 0x05; // Disable interrupt to safely update globals
  pointer = pt;
  arrayCount = count; 
  arrayIndex=0;
  SysTick->CTRL = 0x07; // enable to play
}

// void Sound_Shoot(void){
// // write this
//   Sound_Start(shoot, 4080);
// }
// void Sound_Killed(void){
//   Sound_Start(invaderkilled, 3377);

// }
// void Sound_Explosion(void){
//   Sound_Start(explosion, 2000);
// }

// void Sound_Fastinvader1(void){
//   Sound_Start(fastinvader1, 982);
// }
void Block_Pickup(void){
  Sound_Start(pickup, 400);
}

void Block_Place(void){
  Sound_Start(place, 480);
}
void Block_Reject(void){
  Sound_Start(reject, 600);
}
void Block_Clear(void){
  Sound_Start(clear, 580);
}
