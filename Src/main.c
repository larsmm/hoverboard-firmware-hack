/*
* This file is part of the hoverboard-firmware-hack project.
*
* Copyright (C) 2017-2018 Rene Hopf <renehopf@mac.com>
* Copyright (C) 2017-2018 Nico Stute <crinq@crinq.de>
* Copyright (C) 2017-2018 Niklas Fauth <niklas.fauth@kit.fail>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stm32f1xx_hal.h"
#include "defines.h"
#include "setup.h"
#include "config.h"
#include "comms.h"
#include <stdio.h>
//#include "hd44780.h"

void SystemClock_Config(void);

extern TIM_HandleTypeDef htim_left;
extern TIM_HandleTypeDef htim_right;
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern volatile adc_buf_t adc_buffer;
//LCD_PCF8574_HandleTypeDef lcd;
extern I2C_HandleTypeDef hi2c2;
extern UART_HandleTypeDef huart2;

int cmd1;  // normalized input values. -1000 to 1000
int cmd2;
int cmd3;

typedef struct{
   int16_t steer;
   int16_t speed;
   //uint32_t crc;
} Serialcommand;

volatile Serialcommand command;

uint8_t button1, button2;

int steer; // global variable for steering. -1000 to 1000
int speed; // global variable for speed. -1000 to 1000

extern volatile int pwml;  // global variable for pwm left. -1000 to 1000
extern volatile int pwmr;  // global variable for pwm right. -1000 to 1000
extern volatile int weakl; // global variable for field weakening left. -1000 to 1000
extern volatile int weakr; // global variable for field weakening right. -1000 to 1000

float weak;  // fuer sanftes einsetzen des turbos

extern uint8_t buzzerFreq;    // global variable for the buzzer pitch. can be 1, 2, 3, 4, 5, 6, 7...
extern uint8_t buzzerPattern; // global variable for the buzzer pattern. can be 1, 2, 3, 4, 5, 6, 7...

extern uint8_t enable; // global variable for motor enable

extern volatile uint32_t timeout; // global variable for timeout
extern float batteryVoltage; // global variable for battery voltage

uint32_t inactivity_timeout_counter;

extern uint8_t nunchuck_data[6];
#ifdef CONTROL_PPM
extern volatile uint16_t ppm_captured_value[PPM_NUM_CHANNELS+1];
#endif

int milli_vel_error_sum = 0;


void beep(uint8_t anzahl) {  // blocking function, do not use in main loop!
    for(uint8_t i = 0; i < anzahl; i++) {
        buzzerFreq = 2;
        HAL_Delay(100);
        buzzerFreq = 0;
        HAL_Delay(200);
    }
}


void poweroff() {
    if (ABS(speed) < 20) {
        buzzerPattern = 0;
        enable = 0;
        for (int i = 0; i < 8; i++) {
            buzzerFreq = i;
            HAL_Delay(100);
        }
        HAL_GPIO_WritePin(OFF_PORT, OFF_PIN, 0);
        while(1) {}
    }
}


int main(void) {
  HAL_Init();
  __HAL_RCC_AFIO_CLK_ENABLE();
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
  /* System interrupt init*/
  /* MemoryManagement_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(MemoryManagement_IRQn, 0, 0);
  /* BusFault_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(BusFault_IRQn, 0, 0);
  /* UsageFault_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(UsageFault_IRQn, 0, 0);
  /* SVCall_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SVCall_IRQn, 0, 0);
  /* DebugMonitor_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DebugMonitor_IRQn, 0, 0);
  /* PendSV_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(PendSV_IRQn, 0, 0);
  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  SystemClock_Config();

  __HAL_RCC_DMA1_CLK_DISABLE();
  MX_GPIO_Init();
  MX_TIM_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();

  #if defined(DEBUG_SERIAL_USART2) || defined(DEBUG_SERIAL_USART3)
    UART_Init();
  #endif

  HAL_GPIO_WritePin(OFF_PORT, OFF_PIN, 1);

  HAL_ADC_Start(&hadc1);
  HAL_ADC_Start(&hadc2);

  for (int i = 8; i >= 0; i--) {
    buzzerFreq = i;
    HAL_Delay(100);
  }
  buzzerFreq = 0;

  HAL_GPIO_WritePin(LED_PORT, LED_PIN, 1);

  static int lastSpeedL = 0, lastSpeedR = 0;
  static int speedL = 0, speedR = 0;
  static float speedRL = 0.0;
  static float acc_cmd = 0.0;
  static float brk_cmd = 0.0;
  // float direction = 1;
  
  static float adc1_filtered = ADC1_MIN;
  static float adc2_filtered = ADC2_MIN;

  #ifdef CONTROL_PPM
    PPM_Init();
  #endif

  #ifdef CONTROL_NUNCHUCK
    I2C_Init();
    Nunchuck_Init();
  #endif

  #ifdef CONTROL_SERIAL_USART2
    UART_Control_Init();
    HAL_UART_Receive_DMA(&huart2, (uint8_t *)&command, 4);
  #endif

  #ifdef DEBUG_I2C_LCD
    I2C_Init();
    HAL_Delay(50);
    lcd.pcf8574.PCF_I2C_ADDRESS = 0x27;
      lcd.pcf8574.PCF_I2C_TIMEOUT = 5;
      lcd.pcf8574.i2c = hi2c2;
      lcd.NUMBER_OF_LINES = NUMBER_OF_LINES_2;
      lcd.type = TYPE0;

      if(LCD_Init(&lcd)!=LCD_OK){
          // error occured
          //TODO while(1);
      }

    LCD_ClearDisplay(&lcd);
    HAL_Delay(5);
    LCD_SetLocation(&lcd, 0, 0);
    LCD_WriteString(&lcd, "Hover V2.0");
    LCD_SetLocation(&lcd, 0, 1);
    LCD_WriteString(&lcd, "Initializing...");
  #endif

  printf("\n");
  printf("# hoverboard-firmware-hack larsm's Bobby Car Edition\n");
  printf("# GCC Version: %s\n",__VERSION__);
  printf("# Build Date: %s\n",__DATE__);
  #ifdef CALIBRATION_MODE
  printf("# CALIBRATION_MODE mode on\n");
  #endif
  printf("\n");
  printf("# Mode 1: MAX_SPEED_FORWARDS_M1:%i ACC_FORWARDS_M1:%4.2f MAX_SPEED_BACKWARDS_M1:%i ACC_BACKWARDS_M1:%4.2f\n", MAX_SPEED_FORWARDS_M1, ACC_FORWARDS_M1, MAX_SPEED_BACKWARDS_M1, ACC_BACKWARDS_M1);
  printf("# Mode 2: MAX_SPEED_FORWARDS_M2:%i ACC_FORWARDS_M2:%4.2f MAX_SPEED_BACKWARDS_M2:%i ACC_BACKWARDS_M2:%4.2f\n", MAX_SPEED_FORWARDS_M2, ACC_FORWARDS_M2, MAX_SPEED_BACKWARDS_M2, ACC_BACKWARDS_M2);
  printf("# Mode 3: MAX_SPEED_FORWARDS_M3:%i ACC_FORWARDS_M3:%4.2f MAX_SPEED_BACKWARDS_M3:%i ACC_BACKWARDS_M3:%4.2f\n", MAX_SPEED_FORWARDS_M3, ACC_FORWARDS_M3, MAX_SPEED_BACKWARDS_M3, ACC_BACKWARDS_M3);
  printf("# Mode 4: MAX_SPEED_FORWARDS_M4:%i ACC_FORWARDS_M4:%4.2f MAX_SPEED_BACKWARDS_M4:%i ACC_BACKWARDS_M4:%4.2f\n", MAX_SPEED_FORWARDS_M4, ACC_FORWARDS_M4, MAX_SPEED_BACKWARDS_M4, ACC_BACKWARDS_M4);
  printf("\n");

  // ####### driving modes #######

  // beim einschalten gashebel gedrueckt halten um modus einzustellen:
  // Drive Mode 1, links:     3 kmh, ohne Turbo
  // Drive Mode 2, default:   6 kmh, ohne Turbo
  // Drive Mode 3, rechts:   12 kmh, ohne Turbo
  // Drive Mode 4, l + r:    22 kmh, 29 kmh mit Turbo
  // #ifndef CALIBRATION_MODE
  int16_t start_links  = adc_buffer.l_rx2;  // ADC2, links, rueckwearts, gruen
  int16_t start_rechts = adc_buffer.l_tx2;  // ADC1, rechts, vorwaerts, blau
  static int8_t drive_mode;
  HAL_Delay(300);
  if(start_rechts > (ADC1_MAX - (ADC1_MAX - ADC1_MIN)*0.2) && start_links > (ADC2_MAX - (ADC2_MAX - ADC2_MIN)*0.2)){  // Mode 4
    drive_mode = 4;
    beep(4);
  } else if(start_rechts > (ADC1_MAX - (ADC1_MAX - ADC1_MIN)*0.2)){  // Mode 3
    drive_mode = 3;
    beep(3);
  } else if(start_links > (ADC2_MAX - (ADC2_MAX - ADC2_MIN)*0.2)){  // Mode 1
    drive_mode = 1;
    beep(1);
  } else {  // Mode 2
    drive_mode = 2;
    beep(2);
  }
  printf("# Mode: %i\n", drive_mode);

  #ifndef CALIBRATION_MODE
  printf("# waiting for poti release...\n");
  while(adc_buffer.l_tx2 > (ADC1_MAX - (ADC1_MAX - ADC1_MIN)*0.2) || adc_buffer.l_rx2 > (ADC2_MAX - (ADC2_MAX - ADC2_MIN)*0.2)) HAL_Delay(100); //delay in ms, wait until potis released
  printf("# potis released\n");

  enable = 1;  // enable motors
  #endif

  printf("# enabling motors, entering main loop\n");
  HAL_Delay(20); //delay in ms

  float board_temp_adc_filtered = (float)adc_buffer.temp;
  float board_temp_deg_c;

  while(1) {
    HAL_Delay(DELAY_IN_MAIN_LOOP); //delay in ms

    #ifdef CONTROL_NUNCHUCK
      Nunchuck_Read();
      cmd1 = CLAMP((nunchuck_data[0] - 127) * 8, -1000, 1000); // x - axis. Nunchuck joystick readings range 30 - 230
      cmd2 = CLAMP((nunchuck_data[1] - 128) * 8, -1000, 1000); // y - axis

      button1 = (uint8_t)nunchuck_data[5] & 1;
      button2 = (uint8_t)(nunchuck_data[5] >> 1) & 1;
    #endif

    #ifdef CONTROL_PPM
      cmd1 = CLAMP((ppm_captured_value[0] - 500) * 2, -1000, 1000);
      cmd2 = CLAMP((ppm_captured_value[1] - 500) * 2, -1000, 1000);
      button1 = ppm_captured_value[5] > 500;
      float scale = ppm_captured_value[2] / 1000.0f;
    #endif

    #ifdef CONTROL_ADC
      // ADC values range: 0-4095, see ADC-calibration in config.h
      // cmd1 = CLAMP(adc_buffer.l_tx2 - ADC1_MIN, 0, ADC1_MAX) / (ADC1_MAX / 1000.0f);  // ADC1
      // cmd2 = CLAMP(adc_buffer.l_rx2 - ADC2_MIN, 0, ADC2_MAX) / (ADC2_MAX / 1000.0f);  // ADC2

      // use ADCs as button inputs:
      // button1 = (uint8_t)(adc_buffer.l_tx2 > 2000);  // ADC1
      // button2 = (uint8_t)(adc_buffer.l_rx2 > 2000);  // ADC2

      timeout = 0;
    #endif

    #ifdef CONTROL_SERIAL_USART2
      cmd1 = CLAMP((int16_t)command.steer, -1000, 1000);
      cmd2 = CLAMP((int16_t)command.speed, -1000, 1000);

      timeout = 0;
    #endif


    // ####### larsm's bobby car code #######

    #define INPUT_MAX 1000  // [-] Defines the Input target maximum limitation
    #define INPUT_MIN -1000  // [-] Defines the Input target minimum limitation

    // LOW-PASS FILTER (fliessender Mittelwert)
    adc1_filtered = adc1_filtered * 0.9 + (float)adc_buffer.l_tx2 * 0.1;  // ADC1, TX, rechts, vorwaerts, blau
    adc2_filtered = adc2_filtered * 0.9 + (float)adc_buffer.l_rx2 * 0.1;  // ADC2, RX, links, rueckwearts, gruen

    // poti range normalized from ADC1_MIN - ADC1_MAX to 0.0 - 1.0
    acc_cmd = CLAMP((adc1_filtered - ADC1_MIN) / (ADC1_MAX - ADC1_MIN), 0, 1.0);
    brk_cmd = CLAMP((adc2_filtered - ADC2_MIN) / (ADC2_MAX - ADC2_MIN), 0, 1.0);

    #ifndef CALIBRATION_MODE
    // if poti is significantly out of range: break and poweroff. if MAX or MIN gets too close to 0 or 4095 this feature gets disabled.
    if(adc1_filtered < ((ADC1_MIN < 100) ? 0 : 50) || adc1_filtered > ((ADC1_MAX > 4095-400) ? 4095 : 4095-50) || adc2_filtered < ((ADC2_MIN < 100) ? 0 : 50) || adc2_filtered > ((ADC2_MAX > 4095-400) ? 4095 : 4095-50)){
      acc_cmd = brk_cmd = 0.0;
      if (ABS((int)speedRL) < 5) {  // error beep
        printf("# Poti significantly out of range: power off\n");
        for(uint8_t i = 0; i < 6; i++) {
          buzzerFreq = 6;
          HAL_Delay(50);
          buzzerFreq = 0;
          HAL_Delay(50);
          buzzerFreq = 8;
          HAL_Delay(50);
          buzzerFreq = 0;
          HAL_Delay(50);
        }
        poweroff();
      }
    }
    #endif

    if (drive_mode == 1) {  // Mode 1: 3 km/h@12s
      speedRL = speedRL * (1.0 - (speedRL > 0 ? ACC_FORWARDS_M1/MAX_SPEED_FORWARDS_M1*5.0 : ACC_BACKWARDS_M1/MAX_SPEED_BACKWARDS_M1*5.0))  // breaking if poti is not pressed
              + acc_cmd * ACC_FORWARDS_M1*5.0  // accelerating forwards
              - brk_cmd * ACC_BACKWARDS_M1*5.0;  // accelerating backwards

    } else if (drive_mode == 2) {  // Mode 2: 6 km/h@12s
      speedRL = speedRL * (1.0 - (speedRL > 0 ? ACC_FORWARDS_M2/MAX_SPEED_FORWARDS_M2*5.0 : ACC_BACKWARDS_M2/MAX_SPEED_BACKWARDS_M2*5.0))  // breaking if poti is not pressed
              + acc_cmd * ACC_FORWARDS_M2*5.0  // accelerating forwards
              - brk_cmd * ACC_BACKWARDS_M2*5.0;  // accelerating backwards

    } else if (drive_mode == 3) {  // Mode 3: 12 km/h@12s
      speedRL = speedRL * (1.0 - (speedRL > 0 ? ACC_FORWARDS_M3/MAX_SPEED_FORWARDS_M3*5.0 : ACC_BACKWARDS_M3/MAX_SPEED_BACKWARDS_M3*5.0))  // breaking if poti is not pressed
              + acc_cmd * ACC_FORWARDS_M3*5.0  // accelerating forwards
              - brk_cmd * ACC_BACKWARDS_M3*5.0;  // accelerating backwards

    } else if (drive_mode == 4) {  // Mode 4: without fw: 21 km/h@12s, with fw: 30 km/h@12s
      if(acc_cmd > 0.8 & brk_cmd > 0.8 & speedRL > 0.7 * (float)INPUT_MAX){  // fahrzeug schnell, gas und bremse voll gedrueckt: field weakening
        speedRL = speedRL * (1.0 - (speedRL > 0 ? ACC_FORWARDS_M4/MAX_SPEED_FORWARDS_M4*5.0 : ACC_BACKWARDS_M4/MAX_SPEED_BACKWARDS_M4*5.0))  // breaking if poti is not pressed
                + acc_cmd * ACC_FORWARDS_M4*5.0;  // accelerating forwards
        weak = weak * 0.95 + 400.0 * 0.05;  // sanftes hinzuschalten des field weakening
      } else {  // nur gas gedrueckt: normale fahrt ohne field weakening
        speedRL = speedRL * (1.0 - (speedRL > 0 ? ACC_FORWARDS_M4/MAX_SPEED_FORWARDS_M4*5.0 : ACC_BACKWARDS_M4/MAX_SPEED_BACKWARDS_M4*5.0))  // breaking if poti is not pressed
                + acc_cmd * ACC_FORWARDS_M4*5.0  // accelerating forwards
                - brk_cmd * ACC_BACKWARDS_M4*5.0;  // accelerating backwards
        weak = weak * 0.95;  // sanftes abschalten des field weakening
      }
      weakr = weakl = (int)weak;  // weak should never exceed 400 or 450 MAX!!
    }

    speed = speedR = speedL = CLAMP((int16_t)speedRL, INPUT_MIN, INPUT_MAX);  // clamp output


    // ####### LOW-PASS FILTER #######
    // steer = steer * (1.0 - FILTER) + cmd1 * FILTER;
    // speed = speed * (1.0 - FILTER) + cmd2 * FILTER;


    // ####### MIXER #######
    // speedR = CLAMP(speed * SPEED_COEFFICIENT -  steer * STEER_COEFFICIENT, -1000, 1000);
    // speedL = CLAMP(speed * SPEED_COEFFICIENT +  steer * STEER_COEFFICIENT, -1000, 1000);


    #ifdef ADDITIONAL_CODE
      ADDITIONAL_CODE;
    #endif


    // ####### SET OUTPUTS #######
    if ((speedL < lastSpeedL + 50 && speedL > lastSpeedL - 50) && (speedR < lastSpeedR + 50 && speedR > lastSpeedR - 50) && timeout < TIMEOUT) {
    #ifdef INVERT_R_DIRECTION
      pwmr = speedR;
    #else
      pwmr = -speedR;
    #endif
    #ifdef INVERT_L_DIRECTION
      pwml = -speedL;
    #else
      pwml = speedL;
    #endif
    }

    lastSpeedL = speedL;
    lastSpeedR = speedR;

    if (inactivity_timeout_counter % 30 == 0) {
      // ####### CALC BOARD TEMPERATURE #######
      board_temp_adc_filtered = board_temp_adc_filtered * 0.99 + (float)adc_buffer.temp * 0.01;
      board_temp_deg_c = ((float)TEMP_CAL_HIGH_DEG_C - (float)TEMP_CAL_LOW_DEG_C) / ((float)TEMP_CAL_HIGH_ADC - (float)TEMP_CAL_LOW_ADC) * (board_temp_adc_filtered - (float)TEMP_CAL_LOW_ADC) + (float)TEMP_CAL_LOW_DEG_C;
      
      // ####### DEBUG SERIAL OUT #######
      #ifdef CONTROL_ADC
        setScopeChannel_i(0, (int16_t)adc1_filtered);  // ADC1, TX, rechts, vorwaerts, blau
        setScopeChannel_i(1, (int16_t)adc2_filtered);  // ADC2, RX, links, rueckwearts, gruen
      #endif
      setScopeChannel_f(2, acc_cmd);  // adc1_filtered normalized to 0.0 - 1.0
      setScopeChannel_f(3, brk_cmd);  // adc2_filtered normalized to 0.0 - 1.0
      setScopeChannel_i(4, speed);  // output speed: 0-1000
      setScopeChannel_i(5, (int16_t)weakl);  // field weakening: 0-?
      setScopeChannel_i(6, adc_buffer.batt1);  // for battery voltage calibration
      setScopeChannel_f(7, batteryVoltage);  // for verifying battery voltage calibration
      setScopeChannel_i(8, (int16_t)board_temp_adc_filtered);  // for board temperature calibration
      setScopeChannel_f(9, board_temp_deg_c);  // for verifying board temperature calibration
      consoleScope();
    }


    // ####### POWEROFF BY POWER-BUTTON #######
    if (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) && weakr == 0 && weakl == 0) {
      enable = 0;
      printf("# Power button down, waiting for release...\n");
      while (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN)) {}
      printf("# Power button up: power off\n");
      poweroff();
    }


    // ####### BEEP AND EMERGENCY POWEROFF #######
    if ((TEMP_POWEROFF_ENABLE && board_temp_deg_c >= TEMP_POWEROFF && ABS(speed) < 20) || (batteryVoltage < ((float)BAT_LOW_DEAD * (float)BAT_NUMBER_OF_CELLS) && ABS(speed) < 20)) {  // poweroff before mainboard burns OR low bat 3
      printf("# CPU overtemp or low bat: power off\n");
      poweroff();
    } else if (TEMP_WARNING_ENABLE && board_temp_deg_c >= TEMP_WARNING) {  // beep if mainboard gets hot
      buzzerFreq = 4;
      buzzerPattern = 1;
    } else if (batteryVoltage < ((float)BAT_LOW_LVL1 * (float)BAT_NUMBER_OF_CELLS) && batteryVoltage > ((float)BAT_LOW_LVL2 * (float)BAT_NUMBER_OF_CELLS) && BAT_LOW_LVL1_ENABLE) {  // low bat 1: slow beep
      buzzerFreq = 5;
      buzzerPattern = 42;
    } else if (batteryVoltage < ((float)BAT_LOW_LVL2 * (float)BAT_NUMBER_OF_CELLS) && batteryVoltage > ((float)BAT_LOW_DEAD * (float)BAT_NUMBER_OF_CELLS) && BAT_LOW_LVL2_ENABLE) {  // low bat 2: fast beep
      buzzerFreq = 5;
      buzzerPattern = 6;
    } else if (BEEPS_BACKWARD && speed < -50) {  // backward beep
      buzzerFreq = 5;
      buzzerPattern = 1;
    } else {  // do not beep
      buzzerFreq = 0;
      buzzerPattern = 0;
    }


    // ####### INACTIVITY TIMEOUT #######
    if (ABS(speedL) > 50 || ABS(speedR) > 50) {
      inactivity_timeout_counter = 0;
    } else {
      inactivity_timeout_counter ++;
    }
    if (inactivity_timeout_counter > (INACTIVITY_TIMEOUT * 60 * 1000) / (DELAY_IN_MAIN_LOOP + 1)) {  // rest of main loop needs maybe 1ms
      printf("# inactivity timeout: power off\n");
      poweroff();
    }
  }
}

/** System Clock Configuration
*/
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

  /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL          = RCC_PLL_MUL16;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV8;  // 8 MHz
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

  /**Configure the Systick interrupt time
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);

  /**Configure the Systick
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}
