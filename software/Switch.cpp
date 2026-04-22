/*
 * Switch.cpp
 *
 *  Created on: Nov 5, 2023
 *      Author:
 */
#include <ti/devices/msp/msp.h>
#include "../inc/LaunchPad.h"
// LaunchPad.h defines all the indices into the PINCM table
void Switch_Init(void){
 //pa27 (up) 
  //pa26 (left)
  //pa25 (down)
  //pa24 (right)

  //input enable, software enable, gpio enable for 4 input switch pins
  IOMUX->SECCFG.PINCM[PA27INDEX] = 0x40081;
  IOMUX->SECCFG.PINCM[PA26INDEX] = 0x40081;
  IOMUX->SECCFG.PINCM[PA25INDEX] = 0x40081;
  IOMUX->SECCFG.PINCM[PA24INDEX] = 0x40081;
}

// return current state of switches
uint32_t Switch_In(void){
  uint32_t input = GPIOA->DIN31_0;
  uint32_t mask = (1<<27)|(1<<26)|(1<<25)|(1<<24);
  input &= mask;
  input = input >> 24; // from lsb ->msb, its right-down--left-up
  return input;
  
}
