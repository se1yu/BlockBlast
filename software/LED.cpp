/*
 * LED.cpp
 *
 *  Created on: Nov 5, 2023
 *      Author:
 */
#include <ti/devices/msp/msp.h>
#include "../inc/LaunchPad.h"
// LaunchPad.h defines all the indices into the PINCM table
#define LED_MASK ((1<<17)|(1<<16)|(1<<15)) 
// initialize your LEDs
void LED_Init(void){
    // write this
    // PINCM
    //   bit 25 is HiZ
    //   bit 20 is drive strength
    //   bit 18 is input enable control
    //   bit 17 is pull up control
    //   bit 16 is pull down control
    //   bit 7 is PC peripheral connected, enable transparent data flow
    //   bit 0 selects GPIO function
    
    // --------- 3 LEDS for PA17, PA16, PA15 
      IOMUX->SECCFG.PINCM[PA17INDEX] = (uint32_t) 0x00000081;
      IOMUX->SECCFG.PINCM[PA16INDEX] = (uint32_t) 0x00000081;
      IOMUX->SECCFG.PINCM[PA15INDEX] = (uint32_t) 0x00000081;
    // DOE31_0 Data output enable
      GPIOA->DOE31_0 |= LED_MASK;
      GPIOA->DOUTCLR31_0 = LED_MASK; // LEDs start as off
}
// data specifies which LED to turn on
    //LSB is RIGHT button. if i press right button, turn on led i guess

void LED_On(uint32_t data){
  GPIOA->DOUTSET31_0 = ((data<<15) & LED_MASK);
}

// data specifies which LED to turn off
void LED_Off(uint32_t data){
  GPIOA->DOUTCLR31_0 = (data & LED_MASK);
}

// data specifies which LED to toggle
void LED_Toggle(uint32_t data){
  GPIOA->DOUTTGL31_0 = ((data<<15) & LED_MASK);
}
