// Lab9HMain.cpp
// Runs on MSPM0G3507
// Lab 9 ECE319H
// Jeslyn Chang + Saaketh Manepalli
// Last Modified: 4/21/2026

#include <stdio.h>
#include <stdint.h>
#include <ti/devices/msp/msp.h>
#include "../inc/ST7735.h"
#include "../inc/Clock.h"
#include "../inc/LaunchPad.h"
#include "../inc/TExaS.h"
#include "../inc/Timer.h"
//#include "../inc/SlidePot.h"
#include "../inc/DAC5.h"
#include "SmallFont.h"
#include "LED.h"
#include "Switch.h"
#include "Sound.h"
#include "images\images.h"
extern "C" {
  #include "JoyStick.h"
}

typedef enum {MENU, TUTORIAL, LANG, GAME, SPN_MENU, SPN_TUTORIAL, SPN_LANG, SPN_GAME} GameState_t;
GameState_t state = MENU;

#define LED_MASK ((1<<17)|(1<<16)|(1<<15))

extern "C" void __disable_irq(void);
extern "C" void __enable_irq(void);
extern "C" void TIMG12_IRQHandler(void);
//globals
uint32_t JoyX, JoyY;
uint32_t sw_status;

//   Grid pixel origin:  top-left  = (16, 20)
//    bottom-right = (111, 115)  (8 cols x 8 rows x 12px)
//   Cell size: 12px
//   Grid cells: 8 columns (0-7), 8 rows (0-7)
//
//   ST7735_DrawBitmap(x, y, ptr, w, h)
//     x = left pixel, y = BOTTOM pixel  (ST7735 convention)
//
//   Switch bits (Switch_In() return):
//     bit 0 = right 
//     bit 1 = down
//     bit 2 = left (the main back button)
//     bit 3 = up
//   LED_On(mask). LED_On will shift the bits left to match leds pin 
//   LED_MASK = (1<<17)|(1<<16)|(1<<15)
//   Left LED  = 1<<15, Middle LED = 1<<16, Right LED = 1<<17

//constants
#define GRID_X0 16    // left pixel of grid
#define GRID_Y0 20    // top pixel of grid
#define CELL_SIZE 12    // pixels per cell
#define GRID_COLS 8
#define GRID_ROWS 8

// Switch bit masking
#define SWITCH_UP      0x08  // bit 3
#define SWITCH_DOWN    0x02  // bit 1
#define SWITCH_RIGHT   0x01  // bit 0 (select block in tray)
#define SWITCH_LEFT    0x04  // bit 2 (select block in tray, or back)

// LED masks for each tray slot (left=slot0, mid=slot1, right=slot2)
#define LED_SLOT2  (1<<0)
#define LED_SLOT1  (1<<1)
#define LED_SLOT0  (1<<2)

// score
uint32_t score;  // total cells cleared
uint32_t highScore = 0;

//block definitions
// all blocks are listed by its footprint in cells (cols x rows)
// and its sprite dimensions in pixels
typedef struct {
  const uint16_t *sprite;
  uint8_t spW; // sprite pixel width
  uint8_t spH; // sprite pixel height
  uint8_t cellCols; //footprint columns
  uint8_t cellRows; // footprint rows
} BlockType_t;

// All possible block types (7 total)
#define NUM_BLOCK_TYPES 7
const BlockType_t allBlockTypes[NUM_BLOCK_TYPES] = {
  {single, 12, 12, 1, 1 }, // 1x1
  {miniwide, 24, 12, 2, 1 },//2x1
  {minitall, 12, 24, 1, 2 },// 1x2
  {minisquare, 24, 24, 2, 2 },// 2x2
  {wide, 36, 12, 3, 1 },//3x1
  {tall, 12, 36, 1, 3 },// 1x3
  {square, 36, 36, 3, 3 },// 3x3
};

// Active tray: which type index is in each of the 3 slots
uint8_t traySlotType[3]; // index into allBlockTypes

// Tray draw positions (x=left pixel, y=bottom pixel for DrawBitmap)
const uint8_t trayX[3] = {4, 44, 86};
const uint8_t trayY[3] = {157, 157,157};

//game status variables
// establish the 2X2 array for grid tracking
//thank you omar for the guidance :D
uint8_t grid_matrix[GRID_ROWS][GRID_COLS]; // 0=empty, 1=filled
uint8_t blockAvail[3];// 1=available in tray, 0=already placed

uint8_t traySelection;// 0,1,2 - which tray slot cursor is on
uint8_t holding; // 1 = player has picked up a block
uint8_t holdSlot; // which block type (0,1,2) is being held

//held block position in grid cells (top-left cell of footprint)
int8_t holdCol; // can be -1 if partially off grid (badpixel alignment)
int8_t holdRow;

// Debounce flags for buttons (in addition to existing lastBtn/lastJoy)
uint8_t lastUpBtn;
uint8_t lastDownBtn;
uint8_t lastJoyX;// for left/right joystick (tray selection)

//RNG SETUP
uint32_t M=1;
uint32_t Random32(void){
  M = 1664525*M+1013904223;
  return M;
}
uint32_t Random(uint32_t n){
  return (Random32()>>16)%n;
}

//converting grid -> screen pixel
static inline uint8_t cellToPixX(int8_t col){ return GRID_X0 + col * CELL_SIZE; }
static inline uint8_t cellToPixY(int8_t row){ return GRID_Y0 + row * CELL_SIZE; }

//does it fit in (col, row)?
static uint8_t canPlace(uint8_t slot, int8_t col, int8_t row){
  uint8_t cc = allBlockTypes[traySlotType[slot]].cellCols;
  uint8_t cr = allBlockTypes[traySlotType[slot]].cellRows;
  if (col < 0 || row < 0) return 0;
  if (col + cc > GRID_COLS) return 0;
  if (row + cr > GRID_ROWS) return 0;
  for (uint8_t r = 0; r < cr; r++){
    for (uint8_t c = 0; c < cc; c++){
      if (grid_matrix[row + r][col + c]) return 0;
    }
  }
  return 1;
}

// place block into the grid
static void stampBlock(uint8_t slot, int8_t col, int8_t row){
  uint8_t cc = allBlockTypes[traySlotType[slot]].cellCols;
  uint8_t cr = allBlockTypes[traySlotType[slot]].cellRows;
  for (uint8_t r = 0; r < cr; r++){
    for (uint8_t c = 0; c < cc; c++){
      grid_matrix[row + r][col + c] = 1;
    }
  }
}

//update LEDs to correspond to current selected block.
// Only one LED on at a time; if a slot is empty it stays off
static void updateLEDs(void){
  uint32_t mask = 0;
  if (traySelection == 0 && blockAvail[0]) mask = LED_SLOT0;
  if (traySelection == 1 && blockAvail[1]) mask = LED_SLOT1;
  if (traySelection == 2 && blockAvail[2]) mask = LED_SLOT2;
  LED_Off(LED_MASK);
  if (mask) LED_On(mask);
}

// redrawing the bottom tray (when dirtyFlag = 1)
static void drawTray(void){
  for (uint8_t i = 0; i < 3; i++){
    if (blockAvail[i]){
      ST7735_DrawBitmap(trayX[i], trayY[i],
      allBlockTypes[traySlotType[i]].sprite,
      allBlockTypes[traySlotType[i]].spW,
      allBlockTypes[traySlotType[i]].spH);
    }
  }
}

//draw held block at its current pixel position 
// pixX/pixY = top-left pixel
static void drawHeldBlock(int16_t pixX, int16_t pixY, uint8_t erase){
  uint16_t color = erase ? ST7735_BLACK : 0; // 0 = draw normally via sprite
  if (!erase){
    // DrawBitmap takes x=left, y=bottom
    ST7735_DrawBitmap((int16_t)pixX,
    (int16_t)(pixY + allBlockTypes[traySlotType[holdSlot]].spH - 1),
    allBlockTypes[traySlotType[holdSlot]].sprite,
    allBlockTypes[traySlotType[holdSlot]].spW,
    allBlockTypes[traySlotType[holdSlot]].spH);
  }
  else{
    for (uint8_t r = 0; r < allBlockTypes[traySlotType[holdSlot]].spH; r++){
      for (uint8_t c = 0; c < allBlockTypes[traySlotType[holdSlot]].spW; c++){
        ST7735_DrawPixel(pixX + c, pixY + r, ST7735_BLACK);
      }
    }
  }
}

//score UI 
static void drawScore(void) {
  ST7735_DrawString(0, 0, (char *)"Pts:", ST7735_CYAN);
  ST7735_SetTextColor(ST7735_WHITE);
  ST7735_SetCursor(4, 0);
  ST7735_OutUDec(score);
  ST7735_OutString((char *)"  ");
  ST7735_DrawString(11, 0, (char *)"HI:", ST7735_CYAN);
  ST7735_SetTextColor(ST7735_WHITE);
  ST7735_SetCursor(14, 0);
  ST7735_OutUDec(highScore);
  ST7735_OutString((char *)"    ");
}

//clearing a row/col
static void checkAndClearLines(void){
  uint8_t fullRow[GRID_ROWS] = {0};
  uint8_t fullCol[GRID_COLS] = {0};
  uint8_t anyCleared = 0;

  for (uint8_t r = 0; r < GRID_ROWS; r++){
    uint8_t full = 1;
    for (uint8_t c = 0; c < GRID_COLS; c++)
    if (!grid_matrix[r][c]){ full = 0; break; }
    fullRow[r] = full;
    if (full) anyCleared = 1;
  }
  for (uint8_t c = 0; c < GRID_COLS; c++){
    uint8_t full = 1;
    for (uint8_t r = 0; r < GRID_ROWS; r++)
    if (!grid_matrix[r][c]){ full = 0; break; }
    fullCol[c] = full;
    if (full) anyCleared = 1;
  }

  if (!anyCleared) return;

  uint8_t cellsCleared = 0;
  for (uint8_t r = 0; r < GRID_ROWS; r++)
  for (uint8_t c = 0; c < GRID_COLS; c++)
  if ((fullRow[r] || fullCol[c]) && grid_matrix[r][c]){
    grid_matrix[r][c] = 0;
    cellsCleared++;
  }
  score += cellsCleared;
  Block_Clear();
}

// returns 1 if the given slot can fit anywhere on the current grid
static uint8_t canBlockFitAnywhere(uint8_t slot){
  uint8_t maxCol = GRID_COLS - allBlockTypes[traySlotType[slot]].cellCols;
  uint8_t maxRow = GRID_ROWS - allBlockTypes[traySlotType[slot]].cellRows;
  for (uint8_t r = 0; r <= maxRow; r++)
  for (uint8_t c = 0; c <= maxCol; c++)
  if (canPlace(slot, c, r)) return 1;
  return 0;
}

// returns 1 if no available block in the tray can fit anywhere—> game will b over
static uint8_t isGameOver(void){
  for (uint8_t i = 0; i < 3; i++)
  if (blockAvail[i] && !canBlockFitAnywhere(i)) return 1;
  return 0;
}

//GAME over UI 
static void showGameOver(void) {
  if (score > highScore) highScore = score;
  Clock_Delay1ms(2000);
  ST7735_FillScreen(ST7735_BLACK);
  ST7735_DrawString(2, 2, (char *)"GAME OVER", ST7735_RED);
  // Divider line
  ST7735_DrawFastHLine(0, 32, 128, ST7735_RED);

  // Score in white
  ST7735_DrawString(1, 4, (char *)"Score:", ST7735_CYAN);
  ST7735_SetTextColor(ST7735_WHITE);
  ST7735_SetCursor(7, 4);
  ST7735_OutUDec(score);

  // High score in yellow
  ST7735_DrawString(1, 6, (char *)"Best: ", ST7735_CYAN);
  ST7735_SetTextColor(ST7735_YELLOW);
  ST7735_SetCursor(7, 6);
  ST7735_OutUDec(highScore);

  // Prompt in grey
  ST7735_DrawFastHLine(0, 90, 128, ST7735_WHITE);
  ST7735_DrawString(0, 10, (char *)"Press right btn", ST7735_WHITE);
  ST7735_DrawString(1, 11, (char *)"to play again", ST7735_WHITE);
}

static uint8_t RandomType(void){
  return (uint8_t)((Random32() >> 24) % NUM_BLOCK_TYPES);
}

static void respawnTray(void){
  traySlotType[0] = RandomType();
  traySlotType[1] = RandomType();
  traySlotType[2] = RandomType();
  blockAvail[0] = blockAvail[1] = blockAvail[2] = 1;
  traySelection = 0;
}
// call game_init when entering a game state
static void Game_Init(void){
  for (uint8_t r = 0; r < GRID_ROWS; r++)
  for (uint8_t c = 0; c < GRID_COLS; c++)
  grid_matrix[r][c] = 0;

  M = SysTick->VAL;  // RNG implement
  respawnTray();
  //inits to 0
  holding = 0;
  holdSlot = 0;
  holdCol = 0;
  holdRow = 0;
  lastUpBtn = 0;
  lastDownBtn = 0;
  lastJoyX = 0;
  score = 0; 
  ST7735_DrawBitmap(0, 159, grid, 128, 160);
  drawTray();
  LED_Off(LED_MASK);
  LED_On(LED_SLOT0);
  drawScore();  
}
//called in game whlie loop, should return 1 if should return to MENU (back button held), 0 otherwise
static uint8_t Game_Tick(uint32_t sw, uint32_t jx, uint32_t jy){
  // back button must hold for ~1 second to exit
    static uint8_t backHoldCount = 0;
    if (sw & SWITCH_LEFT){
      backHoldCount++;
      if (backHoldCount >= 30){  // 30 ticks * 33ms = ~1 second
        backHoldCount = 0;
        holding = 0;
        return 1;
      }
    } 
    else{
      backHoldCount = 0;
    }

  // --------------------------------------------------------
  // WHEN NOT HOLDING A BLOCK, can navigate tray
  // ----------------------------------------------
  if (!holding){
    //left
    if (jx > 3000 && lastJoyX == 0){
      if (traySelection > 0) traySelection--;
      lastJoyX = 1;
      updateLEDs();
      drawTray();
    }
    else if (jx < 1000 && lastJoyX == 0){
      //right
      if (traySelection < 2) traySelection++;
      lastJoyX = 1;
      updateLEDs();
      drawTray();
    }
    else if (jx > 1500 && jx < 2500){
      lastJoyX = 0;
    }

    //UP button: pick up the selected block (if available)
    if ((sw & SWITCH_UP) && !lastUpBtn){
      lastUpBtn = 1;
      if (blockAvail[traySelection]){
        holding  = 1;
        holdSlot = traySelection;
        blockAvail[holdSlot] = 0; 
        holdCol = (GRID_COLS / 2) - (allBlockTypes[traySlotType[holdSlot]].cellCols / 2);
        holdRow = (GRID_ROWS / 2) - (allBlockTypes[traySlotType[holdSlot]].cellRows / 2);
        if (holdCol < 0) holdCol = 0;
        if (holdRow < 0) holdRow = 0;
        Block_Pickup();
        drawTray();// redraws tray with that slot now blank
        drawScore();
        drawHeldBlock(cellToPixX(holdCol), cellToPixY(holdRow), 0);
      }
    }
    else if (!(sw & SWITCH_UP)){
      lastUpBtn = 0;
    }

    // --------------------------------------------------------
    // IF HOLDING A BLOCK, move it around
    // --------------------------------------------
  }
  else{
    uint8_t moved = 0;
    int8_t  newCol = holdCol;
    int8_t  newRow = holdRow;
    uint8_t maxCol = GRID_COLS - allBlockTypes[traySlotType[holdSlot]].cellCols;
    uint8_t maxRow = GRID_ROWS - allBlockTypes[traySlotType[holdSlot]].cellRows;
    // x
    if (jx > 3000 && lastJoyX == 0){
      if (newCol > 0) newCol--;
      lastJoyX = 1; moved = 1;
    }
    else if (jx < 1000 && lastJoyX == 0){
      if (newCol < (int8_t)maxCol) newCol++;
      lastJoyX = 1; moved = 1;
    }
    else if (jx > 1500 && jx < 2500){
      lastJoyX = 0;
    }
    //y
    static uint8_t lastJoyY = 0;
    if (jy > 3000 && lastJoyY == 0){
      if (newRow > 0) newRow--;
      lastJoyY = 1; moved = 1;
    }
    else if (jy < 1000 && lastJoyY == 0){
      if (newRow < (int8_t)maxRow) newRow++;
      lastJoyY = 1; moved = 1;
    }
    else if (jy > 1500 && jy < 2500){
      lastJoyY = 0;
    }

    if (moved){
      ST7735_DrawBitmap(0, 159, grid, 128, 160);
      for (uint8_t r = 0; r < GRID_ROWS; r++){
        for (uint8_t c = 0; c < GRID_COLS; c++){
          if (grid_matrix[r][c]){
            uint16_t color = 0x001f;
            for (uint8_t py = 1; py < CELL_SIZE - 1; py++){
              for (uint8_t px = 1; px < CELL_SIZE - 1; px++){
                ST7735_DrawPixel(GRID_X0 + c * CELL_SIZE + px,
                GRID_Y0 + r * CELL_SIZE + py,
                color);
              }
            }
          }
        }
      }
      drawTray();
      holdCol = newCol;
      holdRow = newRow;
      drawHeldBlock(cellToPixX(holdCol), cellToPixY(holdRow), 0);
      drawScore();
    }

    // down to place block
    if ((sw & SWITCH_DOWN) && !lastDownBtn){
      lastDownBtn = 1;
      if (canPlace(holdSlot, holdCol, holdRow)){
        // valid placement!
        stampBlock(holdSlot, holdCol, holdRow);
        Block_Place();
        holding = 0;
        checkAndClearLines();
        // redraw game screen
        ST7735_DrawBitmap(0, 159, grid, 128, 160);
        // redraw placed blocks
        for (uint8_t r = 0; r < GRID_ROWS; r++){
          for (uint8_t c = 0; c < GRID_COLS; c++){
            if (grid_matrix[r][c]){
              uint16_t color = 0x001f;
              for (uint8_t py = 1; py < CELL_SIZE - 1; py++){
                for (uint8_t px = 1; px < CELL_SIZE - 1; px++){
                  ST7735_DrawPixel(GRID_X0 + c * CELL_SIZE + px,
                  GRID_Y0 + r * CELL_SIZE + py,
                  color);
                }
              }
            }
          }
        }
        drawTray();
        updateLEDs();
        drawScore();
        // respawn tray when all 3 blocks placed
        uint8_t allPlaced = (!blockAvail[0] && !blockAvail[1] && !blockAvail[2]);
        if (allPlaced){
          respawnTray();
          updateLEDs();
          drawTray();
        }
        // check game over after every placement (and after respawn)
        if (isGameOver()){
          showGameOver();
          //wait for the btn to restart
          while (!(sw_status & SWITCH_RIGHT)){}
          Game_Init();
          return 0;
        }
      }
      else{
        //invalid placement
        Block_Reject();
      }
    }
    else if (!(sw & SWITCH_DOWN)){
      lastDownBtn = 0;
    }

    //cancelling pickup if up btn while in hovering mode
    if ((sw & SWITCH_UP) && !lastUpBtn){
      lastUpBtn = 1;
      blockAvail[holdSlot] = 1; // restore to tray
      drawHeldBlock(cellToPixX(holdCol), cellToPixY(holdRow), 1);
      ST7735_DrawBitmap(0, 159, grid, 128, 160);
      // redraw placed cells
      for (uint8_t r = 0; r < GRID_ROWS; r++){
        for (uint8_t c = 0; c < GRID_COLS; c++){
          if (grid_matrix[r][c]){
            uint16_t color = 0x001f;
            for (uint8_t py = 1; py < CELL_SIZE - 1; py++){
              for (uint8_t px = 1; px < CELL_SIZE - 1; px++){
                ST7735_DrawPixel(GRID_X0 + c * CELL_SIZE + px,
                GRID_Y0 + r * CELL_SIZE + py,
                color);
              }
            }
          }
        }
      }
      holding = 0;
      drawTray();
      updateLEDs();
    }
    else if (!(sw & SWITCH_UP)){
      lastUpBtn = 0;
    }
  }

  return 0;
}

// ****note to ECE319K students****
// the data sheet says the ADC does not work when clock is 80 MHz
// however, the ADC seems to work on my boards at 80 MHz
// I suggest you try 80MHz, but if it doesn't work, switch to 40MHz
void PLL_Init(void){ // set phase lock loop (PLL)
//Clock_Init40MHz(); // run this line for 40MHz
Clock_Init80MHz(0);   // run this line for 80MHz
}

//SlidePot Sensor(1769,13); //my callibration for one pot

// games  engine runs at 30Hz
//ISR
void TIMG12_IRQHandler(void){uint32_t pos,msg;
if((TIMG12->CPU_INT.IIDX) == 1){ // this will acknowledge
GPIOB->DOUTTGL31_0 = GREEN; // toggle PB27 (minimally intrusive debugging)
GPIOB->DOUTTGL31_0 = GREEN; // toggle PB27 (minimally intrusive debugging)
// game engine goes here
// 1) sample joystick
JoyStick_In(&JoyX, &JoyY);
// 2) read input switches
sw_status = Switch_In();
// 3) move sprites
// 4) start sounds
// 5) set semaphore
// NO LCD OUTPUT IN INTERRUPT SERVICE ROUTINES
GPIOB->DOUTTGL31_0 = GREEN; // toggle PB27 (minimally intrusive debugging)
}
}
uint8_t TExaS_LaunchPadLogicPB27PB26(void){
  return (0x80|((GPIOB->DOUT31_0>>26)&0x03));
}

typedef enum {English, Spanish, Portuguese, French} Language_t;
Language_t myLanguage=English;
typedef enum {HELLO, GOODBYE, LANGUAGE} phrase_t;
const char Play[] = "Play";
const char Tutorial_Text[] = "LED lit up corresponds \n with the block selected \n UP = pickup block/deselect block \n DOWN = place block \n Game will end when there are no possible moves left.";
const char Tutorial[] = "Tutorial";
const char Language[] = "Language";

//Spanish
const char Spn_Play[] = "Jugar";
const char Spn_Tutorial_Text[] = "El LED encendido corresponde \n con el bloque seleccionado \n ARRIBA = recoger bloque/deseleccionar bloque \n ABAJO = colocar bloque \n El juego terminará cuando no haya más movimientos posibles.";
const char Spn_Tutorial[] = "Tutorial";
const char Spn_Language[] = "Idioma";

const char Hello_English[] ="Hello";
const char Hello_Spanish[] ="\xADHola!";
const char Hello_Portuguese[] = "Ol\xA0";
const char Hello_French[] ="All\x83";
const char Goodbye_English[]="Goodbye";
const char Goodbye_Spanish[]="Adi\xA2s";
const char Goodbye_Portuguese[] = "Tchau";
const char Goodbye_French[] = "Au revoir";
const char Language_English[]="English";
const char Language_Spanish[]="Espa\xA4ol";
const char Language_Portuguese[]="Portugu\x88s";
const char Language_French[]="Fran\x87" "ais";
const char *Phrases[3][4]={
  {Hello_English,Hello_Spanish,Hello_Portuguese,Hello_French},
  {Goodbye_English,Goodbye_Spanish,Goodbye_Portuguese,Goodbye_French},
  {Language_English,Language_Spanish,Language_Portuguese,Language_French}
};
// use main1 to observe special characters
int main1(void){ // main1
  char l;
  __disable_irq();
  PLL_Init(); // set bus speed
  LaunchPad_Init();
  ST7735_InitPrintf(INITR_BLACKTAB); // INITR_REDTAB for AdaFruit, INITR_BLACKTAB for HiLetGo
  ST7735_FillScreen(0x0000);            // set screen to black
  for(int myPhrase=0; myPhrase<= 2; myPhrase++){
    for(int myL=0; myL<= 3; myL++){
      ST7735_OutString((char *)Phrases[LANGUAGE][myL]);
      ST7735_OutChar(' ');
      ST7735_OutString((char *)Phrases[myPhrase][myL]);
      ST7735_OutChar(13);
    }
  }

  Clock_Delay1ms(3000);
  ST7735_FillScreen(0x0000);       // set screen to black
  l = 128;
  while(1){
    Clock_Delay1ms(2000);
    for(int j=0; j < 3; j++){
      for(int i=0;i<16;i++){
        ST7735_SetCursor(7*j+0,i);
        ST7735_OutUDec(l);
        ST7735_OutChar(' ');
        ST7735_OutChar(' ');
        ST7735_SetCursor(7*j+4,i);
        ST7735_OutChar(l);
        l++;
      }
    }
  }
}



// use main2 to observe graphics
int main2(void){ // main2
  __disable_irq();
  PLL_Init(); // set bus speed
  LaunchPad_Init();
  ST7735_InitPrintf(INITR_BLACKTAB); // INITR_REDTAB for AdaFruit, INITR_BLACKTAB for HiLetGo
  ST7735_FillScreen(ST7735_BLACK);
  ST7735_DrawBitmap(0, 159, Home, 128, 160); // player ship bottom
  ST7735_DrawBitmap(53, 151, L_block, 40,40);
  ST7735_DrawBitmap(42, 159, PlayerShip1, 18,8); // player ship bottom
  ST7735_DrawBitmap(62, 159, PlayerShip2, 18,8); // player ship bottom
  ST7735_DrawBitmap(82, 159, PlayerShip3, 18,8); // player ship bottom
  ST7735_DrawBitmap(0, 9, SmallEnemy10pointA, 16,10);
  ST7735_DrawBitmap(20,9, SmallEnemy10pointB, 16,10);
  ST7735_DrawBitmap(40, 9, SmallEnemy20pointA, 16,10);
  ST7735_DrawBitmap(60, 9, SmallEnemy20pointB, 16,10);
  ST7735_DrawBitmap(80, 9, SmallEnemy30pointA, 16,10);

  for(uint32_t t=500;t>0;t=t-5){
    SmallFont_OutVertical(t,104,6); // top left
    Clock_Delay1ms(50);              // delay 50 msec
  }
  ST7735_FillScreen(0x0000);   // set screen to black
  ST7735_SetCursor(1, 1);
  ST7735_OutString((char *)"GAME OVER");
  ST7735_SetCursor(1, 2);
  ST7735_OutString((char *)"Nice try,");
  ST7735_SetCursor(1, 3);
  ST7735_OutString((char *)"Earthling!");
  ST7735_SetCursor(2, 4);
  ST7735_OutUDec(1234);
  while(1){
  }
}

// use main3 to test switches and LEDs
int main3(void){ // main3
  __disable_irq();
  PLL_Init(); // set bus speed
  LaunchPad_Init();
  Switch_Init(); // initialize switches
  LED_Init(); // initialize LED
  while(1){
    uint32_t status = Switch_In(); // Returns bits 0-3
    LED_Off(LED_MASK);
    LED_On(status);
    Clock_Delay(800000); // 10ms, to debounce switch
  }
}

// use main4 to test sound outputs
int main4(void){ uint32_t last=0,now;
  __disable_irq();
  PLL_Init(); // set bus speed
  LaunchPad_Init();
  Switch_Init(); // initialize switches
  LED_Init(); // initialize LED
  Sound_Init();  // initialize sound
  TExaS_Init(ADC0,6,0); // ADC1 channel 6 is PB20, TExaS scope
  __enable_irq();
  while(1){
    now = (Switch_In() & (0x0F)); // from lsb->msb:  its right-down--left-up
    if((last == 0)&&(now == 1)){
      Block_Pickup(); // call one of your sounds
    }
    if((last == 0)&&(now == 2)){
      Block_Place(); // call one of your sounds
    }
    if((last == 0)&&(now == 4)){
      Block_Reject(); // call one of your sounds
    }
    if((last == 0)&&(now == 8)){
      Block_Clear(); // call one of your sounds
    }
    last = now;
  }
}


// ALL ST7735 OUTPUT MUST OCCUR IN MAIN
int main(void){ //main 5 final
  __disable_irq();
  PLL_Init();
  LaunchPad_Init();
  ST7735_InitPrintf(INITR_BLACKTAB); // INITR_REDTAB for AdaFruit, INITR_BLACKTAB for HiLetGo
  //initialize everything
  JoyStick_Init();
  Switch_Init(); // initialize switches
  LED_Init();    // initialize LED
  Sound_Init();  // initialize sound
  TExaS_Init(0,0,&TExaS_LaunchPadLogicPB27PB26); // PB27 and PB26
  // initialize interrupts on TimerG12 at 30 Hz
  TimerG12_IntArm(80000000/30,2);
  __enable_irq();

  //redefine state enum on local scope
  typedef enum { MENU, TUTORIAL, GAME } GameState_t;
  GameState_t state = MENU;

  uint32_t lang = 0;
  uint32_t menuSelection = 0;
  uint32_t lastJoy = 0;
  uint32_t lastBtn = 0;
  uint8_t dirty = 1;

  const char *menuPlay[]= {"Play","Jugar"};
  const char *menuTutorial[] = {"Tutorial", "Tutorial"};
  const char *menuLang[] = {"Language", "Idioma"};
  const char *tutorialText[] = {Tutorial_Text, Spn_Tutorial_Text};

  while (1){
    uint32_t sw = sw_status;
    uint32_t jx = JoyX;
    uint32_t jy = JoyY;

    // Menu
    if (state == MENU){
      // Joystick up/down navigates menu
      if (jy > 3000 && lastJoy == 0){
        if (menuSelection > 0) menuSelection--;
        lastJoy = 1; dirty = 1;
      }
      else if (jy < 1000 && lastJoy == 0){
        if (menuSelection < 2) menuSelection++;
        lastJoy = 1; dirty = 1;
      }
      else if (jy > 1500 && jy < 2500){
        lastJoy = 0;
      }
      // Right button selects
      if ((sw & SWITCH_RIGHT) && !lastBtn){
        lastBtn = 1;
        if (menuSelection == 0){
          state = GAME;
          Game_Init(); // initialize grid, LEDs, draw game screen
          dirty = 0;// Game_Init handles its own first draw
        }
        else if (menuSelection == 1){
          state = TUTORIAL;
          dirty = 1;
        }
        else if (menuSelection == 2){
          lang = (lang + 1) % 2;
          dirty = 1;
        }
      }
      else if (!(sw & SWITCH_RIGHT) && !(sw & SWITCH_LEFT)){
        lastBtn = 0;
      }

      //draw menu if dirty & we are  still in the MENU state
      if (dirty && state == MENU){
        dirty = 0;
        ST7735_DrawBitmap(0, 159, Home, 128, 160);
        uint16_t c0 = (menuSelection == 0) ? ST7735_YELLOW : ST7735_WHITE;
        uint16_t c1 = (menuSelection == 1) ? ST7735_YELLOW : ST7735_WHITE;
        uint16_t c2 = (menuSelection == 2) ? ST7735_YELLOW : ST7735_WHITE;
        ST7735_DrawString(4, 10, (char *)(menuSelection == 0 ? ">" : " "), c0);
        ST7735_DrawString(5, 10, (char *)menuPlay[lang], c0);
        ST7735_DrawString(4, 11, (char *)(menuSelection == 1 ? ">" : " "), c1);
        ST7735_DrawString(5, 11, (char *)menuTutorial[lang], c1);
        ST7735_DrawString(4, 12, (char *)(menuSelection == 2 ? ">" : " "), c2);
        ST7735_DrawString(5, 12, (char *)menuLang[lang], c2);
      }

      // TUTORIAL state
      // -------------------------------------
    }
    else if (state == TUTORIAL){
      // CRITICAL: Reset joystick if user lets go while in the tutorial
      // This fixes the joystick breaking when returning to the menu
      if (jy > 1500 && jy < 2500){
        lastJoy = 0;
      }

      //left btn as back 
      if ((sw & SWITCH_LEFT) && !lastBtn){
        lastBtn = 1;
        state = MENU;
        dirty = 1;
      }
      else if (!(sw & SWITCH_LEFT) && !(sw & SWITCH_RIGHT)){
        lastBtn = 0;
      }

      // Draw tutorial if dirty AND we are actually still in the TUTORIAL state
      if (dirty && state == TUTORIAL){
        dirty = 0;
        ST7735_FillScreen(ST7735_BLACK);
        ST7735_DrawFastHLine(0, 10, 128, ST7735_CYAN);
        ST7735_DrawString(3, 0, (char *)"TUTORIAL", ST7735_CYAN);
        ST7735_DrawFastHLine(0, 10, 128, ST7735_CYAN);
        ST7735_SetTextColor(ST7735_WHITE);
        ST7735_SetCursor(0, 2);
        ST7735_OutString((char *)tutorialText[lang]);
        ST7735_DrawString(0, 15, (char *)"< back", ST7735_WHITE);
      }
    }
    // GAME state
    // ---------------------------
    else if (state == GAME){
      // Game_Tick handles all input, drawing, LEDs.
      // Returns 1 if player pressed back -> return to menu.
      uint8_t goBack = Game_Tick(sw, jx, jy);
      if (goBack){
        state = MENU;
        LED_Off(LED_MASK);
        dirty = 1;  // menu needs redraw next iteration
      }
    }
    Clock_Delay1ms(33); // ~30 Hz main loop (ISR also runs at 30 Hz)
  }
}

