# BlockBlast! 
- Demo Link: https://youtu.be/8z5zvf6NJ1U?si=ciNaijVOC2jGjhIF

Using knowledge from ECE 319H at UT Austin (Intro to Embedded Systems) to build a custom video game console and to program Block Blast from scratch. 

Block Blast is a puzzle game I implemented for a MSPM0G3507 microcontroller, displayed on a 128×160 ST7735 Liquid Crystal Display (LCD) screen. The player is presented with an 8×8 grid and a tray of three randomly selected blocks. Using a joystick and four external buttons, the player picks up blocks from the tray, moves them around the grid, and drops them into position. When an entire row or column of the grid becomes filled, it clears and the player earns points. The game ends when any remaining block in the tray has no valid position anywhere on the board. The game supports English and Spanish, tracks a persistent high score across rounds, and features four distinct sound effects triggered by gameplay events.
# Images
<img width="300" alt="img_1748" src="https://github.com/user-attachments/assets/5419ddf8-1ff4-4b4a-87b8-da2be4cd3b30" />
<img width="300" alt="img_1751" src="https://github.com/user-attachments/assets/b68a333e-e006-4b04-a846-a5f4149e091e" />
<img width="300" alt="img_1343" src="https://github.com/user-attachments/assets/698196d8-c800-4f63-ac83-2e0ea46bf643" />
<img width="300" alt="img_1342" src="https://github.com/user-attachments/assets/314fa653-04ff-48c4-b15d-26e00f44e692" />
<img width="300" alt="img_1750" src="https://github.com/user-attachments/assets/605a7a6c-8a94-4a3e-bcce-f4d14ef7b2ec" />

--------

# System Architecture
There are two ISRs running concurrently:
Armed with TimerG12_IntArm(80000000/30, 2), which fires an interrupt every 30ms. It does three things each tick:
- Calls JoyStick_In(&JoyX, &JoyY) — which internally triggers ADC_InDual (analog-to-digital-convertor) on ADC1 channels 4 and 6 (PB17 and PB19), sampling both joystick axes simultaneously via a dual-channel 12-bit ADC conversion. According to the Nyquist Theorem, an input should be sampled at a frequency at least twice the maximum input frequency. 30Hz (30 times a second) sampling is well above the maximum frequency a human wrist can move a joystick!
- Calls Switch_In() — reads the four external buttons wired to PA24–PA27, returning a 4-bit value where each bit represents one directional button that is currently being pressed (right/down/left/up). 
- Stores results in the global variables JoyX, JoyY, and sw_status for the main program to access these external hardware updates.

Regarding Audio, we have the SysTick_Handler — Audio DAC at 11 kHz:
Armed with SysTick_IntArm(80000000/11000, 0), this fires every ~90µs and drives the 5-bit binary-weighted DAC on PB0–PB4. It maintains three global variables: pointer to sound array(values to be played to output sound), arrayCount (total # of bytes per sound array), and arrayIndex (current position). Each tick it outputs pointer[arrayIndex] to DAC5_Out() and increments the index. When the array is exhausted it disables the interrupt. Sound is started by calling Sound_Start(pt, count) which resets the index and re-enables the interrupt. This ISR runs at higher priority (0) than the game timer (2) so audio never stutters during gameplay.

# ADC & Joystick
JoyStick_Init() calls ADC_InitDual(ADC1, 4, 6, ADCVREF_VDDA), configuring ADC1 to sequentially sample two channels in one software trigger: channel 4 (PB17, horizontal) and channel 6 (PB19, vertical). ADC_InDual triggers the conversion and "busy-waits" for both results, returning values 0–4095 for each axis. The neutral position is approximately 2048. In Game_Tick, the bounding numbers of 1000 and 3000 define deflection zones — below 1000 means pushed one way, above 3000 means the other. The joystick counts as two slide potentiometers sampled periodically by the ADC at 30 Hz inside the timer ISR.

# Switches/Buttons
Switch_Init() configures PA24–PA27 as digital inputs with PINCM = 0x40081 (input enable + pull-up). Switch_In() reads GPIOA->DIN31_0, masks bits 24–27, and right-shifts by 24 to produce a clean 4-bit value. These are four external buttons wired:
- Bit 0 = right (select / confirm)
- Bit 1 = down (place block)
- Bit 2 = left (back / cancel, requires ~1 second hold on game screen to go backwards)
- Bit 3 = up (pick up block / cancel pickup)

# Sound
Four gameplay sounds are defined in sounds.h as const uint8_t arrays of 5-bit DAC samples recorded at 11 kHz:
- Block_Pickup() — plays when the player picks up a block from the tray
- Block_Place() — plays on successful block placement
- Block_Reject() — plays when a drop is attempted in an invalid position
- Block_Clear() — plays when one or more rows or columns are cleared
Each calls Sound_Start(array, count) which loads the pointer/counter globals and enables the SysTick interrupt. The DAC is a 5-bit R-2R ladder on PB0–PB4, giving 32 voltage levels from 0 to 3.3V.

# Sprites & Graphics
All sprites I drew in Microsoft Paint are stored as const uint16_t arrays in images.h in RGB565 format (16 bit). ST7735_DrawBitmap(x, y, ptr, w, h) renders them with y as the bottom pixel edge. The active sprites in gameplay are:
- Home (128×160) — the menu/title screen background
- grid (128×160) — the full game screen including the 8×8 grid and tray area background
- single (12×12) — 1×1 cell
- miniwide (24×12) — 2×1 cells
- minitall (12×24) — 1×2 cells
- minisquare (24×24) — 2×2 cells
- wide (36×12) — 3×1 cells
- tall (12×36) — 1×3 cells
- square (36×36) — 3×3 cells
Moving sprite: The held block moves across the grid in 12px increments (one cell) responding to joystick deflection. On each move, ST7735_DrawBitmap(grid) redraws the background, all filled cells are repainted as solid 10×10px colored squares inside each 12×12 cell, the tray is redrawn, and finally the held block is drawn at its new position.

# Grid & Game Logic
The grid state is a uint8_t grid_matrix[8][8] where 0 = empty and 1 = filled. Block types are described by a BlockType_t struct containing the sprite pointer, pixel dimensions, and cell footprint. The active tray is traySlotType[3] — indices into allBlockTypes[7] — randomized each respawn using a linear congruential generator from SysTick->VAL at game start for true randomness. Random32() implements a linear congruential generator: M = 1664525*M + 1013904223. RandomType() uses (Random32() >> 24) % 7 — taking bits 31–24 for a cycle length of 2²⁴ rather than the short-cycling low bits. The seed M = SysTick->VAL is set once at Game_Init(), capturing the hardware counter at the moment the player starts the game, ensuring a different block sequence every round.

The canPlace(slot, col, row) checks bounds and scans the footprint against the matrix. stampBlock writes 1s into the matrix. checkAndClearLines() after every placement scans all 8 rows and 8 columns, marks full ones, then zeroes any cell whose row or column was full — handling intersections correctly without double-counting. isGameOver() iterates all available tray blocks and calls canBlockFitAnywhere() which scans every valid grid position; if any available block has zero valid placements, the game ends.

# LEDs 
Three LEDs on PA15–PA17 indicate which tray slot is currently selected. LED_On(data) left-shifts data by 15 to align with the GPIO pins. Only the LED corresponding to traySelection lights up, and it goes dark when that slot's block is picked up or placed.

# Language Support 
All user-facing strings exist in both English and Spanish. A lang variable (0 = English, 1 = Spanish) indexes into parallel string arrays for menu labels, tutorial text, and prompts. The language cycles with the Language menu option and persists through the game session.


