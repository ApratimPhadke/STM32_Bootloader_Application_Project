/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#define APP_ADDRESS 0x08004000
//#define FLASH_END 0x08080000
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t crc;
    uint32_t version;
    uint32_t state;
} firmware_header_t;

/* State flag values written into firmware_header_t.state */
#define FIRMWARE_MAGIC           0xDEADBEEF
#define FIRMWARE_STATE_UPDATING  0xAAAAAAAA
#define FIRMWARE_STATE_VALID     0x12345678

/* Address where the firmware header lives (start of application area) */
#define FIRMWARE_HEADER_ADDRESS  APP_ADDRESS

/* Address where actual application code starts (right after the header) */
#define APP_CODE_ADDRESS         (APP_ADDRESS + sizeof(firmware_header_t))

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
uint8_t size;
uint8_t data[64];
uint8_t checksum;
uint32_t flash_write_address = 0x08004000;

/* Running CRC accumulator — updated after every packet is written to flash */
uint32_t firmware_crc = 0xFFFFFFFF;

/* Running byte counter — total firmware bytes received so far */
uint32_t firmware_size = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
uint8_t receive_packet(void);
uint8_t calc_checksum(uint8_t *data, uint8_t size);
void flash_erase(void);
void flash_write(uint8_t *data, uint8_t size);

/* Fail-safe helpers */
void     flash_write_state(uint32_t state_value);
void     flash_finalize_header(void);
uint8_t  firmware_is_valid(void);
uint32_t crc32_update(uint32_t crc, uint8_t *buf, uint32_t len);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
//void jump_to_app(void)
//{
//    uint32_t app_sp    = *(volatile uint32_t*)APP_ADDRESS;
//    uint32_t app_reset = *(volatile uint32_t*)(APP_ADDRESS + 4);
//
//    void (*app_entry)(void);
//
//    __disable_irq();
//
//    HAL_DeInit();
//    HAL_RCC_DeInit();
//
//    SCB->VTOR = APP_ADDRESS;   // vector table relocation
//
//    __set_MSP(app_sp);
//
//    app_entry = (void (*)(void))app_reset;
//    app_entry();
//    if ((*(uint32_t*)APP_ADDRESS & 0x2FFE0000) != 0x20000000)
//    {
//        return; // invalid app
//    }
//}
void jump_to_app(void)
{

    if ((*(uint32_t*)APP_ADDRESS & 0x2FFE0000) != 0x20000000)
    {
        return; // invalid app
    }

    uint32_t app_sp    = *(volatile uint32_t*)APP_ADDRESS;
    uint32_t app_reset = *(volatile uint32_t*)(APP_ADDRESS + 4);

    void (*app_entry)(void);

    __disable_irq();

    HAL_DeInit();
    HAL_RCC_DeInit();

    SCB->VTOR = APP_ADDRESS;

    __set_MSP(app_sp);

    app_entry = (void (*)(void))app_reset;
    app_entry();
}

/* ---------------------------------------------------------------------------
 * crc32_update()
 * CRC32 (IEEE 802.3 polynomial 0xEDB88320, reflected).
 * Used to verify firmware integrity before and after update.
 * Initial value should be 0xFFFFFFFF; final value should be XORed with 0xFFFFFFFF.
 * --------------------------------------------------------------------------*/
uint32_t crc32_update(uint32_t crc, uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc;
}


void flash_write_state(uint32_t state_value)
{
    /* Offset of the 'state' field inside firmware_header_t */
    uint32_t state_offset = offsetof(firmware_header_t, state);
    uint32_t state_addr   = FIRMWARE_HEADER_ADDRESS + state_offset;

    HAL_FLASH_Unlock();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, state_addr, state_value);
    HAL_FLASH_Lock();
}

/* ---------------------------------------------------------------------------
 * flash_finalize_header()
 * Called after all firmware packets have been received and CRC is verified.
 * Writes the complete firmware_header_t (magic, size, crc, version, state=VALID)
 * into flash at APP_ADDRESS.
 *
 * Layout in flash after this call:
 *   [0x08004000] firmware_header_t  (20 bytes)
 *   [0x08004014] actual firmware binary
 * --------------------------------------------------------------------------*/
void flash_finalize_header(void)
{
    firmware_header_t hdr;
    hdr.magic   = FIRMWARE_MAGIC;
    hdr.size    = firmware_size;
    hdr.crc     = firmware_crc ^ 0xFFFFFFFF;  /* finalize CRC */
    hdr.version = 1;
    hdr.state   = FIRMWARE_STATE_VALID;

    HAL_FLASH_Unlock();

    uint8_t *p = (uint8_t *)&hdr;
    uint32_t addr = FIRMWARE_HEADER_ADDRESS;

    for (uint32_t i = 0; i < sizeof(firmware_header_t); i += 4)
    {
        uint32_t word = 0xFFFFFFFF;
        for (int j = 0; j < 4 && (i + j) < sizeof(firmware_header_t); j++)
            ((uint8_t *)&word)[j] = p[i + j];
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
        addr += 4;
    }

    HAL_FLASH_Lock();
}

/* ---------------------------------------------------------------------------
 * firmware_is_valid()
 * Reads the firmware_header_t from flash and performs all validation checks:
 *   1. Magic number check
 *   2. State flag check  — must be FIRMWARE_STATE_VALID (not UPDATING / erased)
 *   3. Size sanity check — must be > 0 and fit within available flash
 *   4. CRC verification  — recomputes CRC over firmware bytes and compares
 *
 * Returns 1 if firmware is safe to boot, 0 otherwise.
 * The bootloader will NEVER jump unless this returns 1.
 * --------------------------------------------------------------------------*/
uint8_t firmware_is_valid(void)
{
    firmware_header_t *hdr = (firmware_header_t *)FIRMWARE_HEADER_ADDRESS;

    /* Check 1: Magic number — confirms header was intentionally written */
    if (hdr->magic != FIRMWARE_MAGIC)
        return 0;

    /* Check 2: State flag — must be VALID, not UPDATING or erased (0xFFFFFFFF) */
    if (hdr->state != FIRMWARE_STATE_VALID)
        return 0;

    /* Check 3: Size sanity — firmware must have at least 1 byte and
     *          fit inside the flash area reserved for the application */
    uint32_t max_size = FLASH_END - APP_CODE_ADDRESS;
    if (hdr->size == 0 || hdr->size > max_size)
        return 0;

    /* Check 4: CRC verification — recompute over the actual firmware bytes */
    uint32_t computed = 0xFFFFFFFF;
    computed = crc32_update(computed, (uint8_t *)APP_CODE_ADDRESS, hdr->size);
    computed ^= 0xFFFFFFFF;  /* finalize */

    if (computed != hdr->crc)
        return 0;

    /* All checks passed — firmware is valid */
    return 1;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  //flash_erase();
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)
  {
      // BOOTLOADER MODE (button pressed)

      static uint8_t erased = 0;

      /* Reset CRC and size counters for this update session */
      firmware_crc  = 0xFFFFFFFF;
      firmware_size = 0;

      /* Move write pointer past the header so firmware code starts at APP_CODE_ADDRESS.
       * The header itself will be written last by flash_finalize_header(). */
      flash_write_address = APP_CODE_ADDRESS;

      while (1)
      {
          if (!erased)
          {
              flash_erase();
              erased = 1;

              /* Mark update as IN PROGRESS immediately after erase.
               * If power is lost before flash_finalize_header() is called,
               * firmware_is_valid() will see STATE=UPDATING and refuse to jump. */
              flash_write_state(FIRMWARE_STATE_UPDATING);
          }

          if (receive_packet())
          {
              HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

              /* Accumulate CRC and size before writing so we can verify later */
              firmware_crc   = crc32_update(firmware_crc, data, size);
              firmware_size += size;

              flash_write(data, size);
          }
      }

      /* NOTE: flash_finalize_header() is called by receive_packet() when it
       * receives an end-of-firmware sentinel packet (size == 0).
       * Until that happens the state stays UPDATING and the device will not boot
       * the new firmware even if reset occurs. */
  }

  else
  {
      /* APPLICATION MODE — only jump if firmware passes all validation checks */
      if (firmware_is_valid())
      {
          jump_to_app();
      }

      // fallback if jump fails
      while (1)
      {
          HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
          HAL_Delay(200);
      }
  }
  /* USER CODE END 2 */
  while (1)
    {
    }

} /* closing brace for main() */




  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
//  while (1)
//  {
//    /* USER CODE END WHILE */
//	  if (receive_packet())
//	      {
//	          HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
//	          flash_write(data, size);
//	      }
//
//    /* USER CODE BEGIN 3 */
//  }
//  /* USER CODE END 3 */
//}
//  static uint8_t erased = 0;
//
//  while (1)
//  {
//      if (!erased)
//      {
//          flash_erase();
//          erased = 1;
//      }
//
//      if (receive_packet())
//      {
//          HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
//          flash_write(data, size);
//      }
//  }
//}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
	huart1.Instance = USART1;
	huart1.Init.BaudRate = 115200;
	huart1.Init.WordLength = UART_WORDLENGTH_8B;
	huart1.Init.StopBits = UART_STOPBITS_1;
	huart1.Init.Parity = UART_PARITY_NONE;
	huart1.Init.Mode = UART_MODE_TX_RX;
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Configure PA0 as input (bootloader trigger button) */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
uint8_t calc_checksum(uint8_t *data,uint8_t size){
	uint8_t sum =0;
	for(int i =0;i<size;i++){
		sum +=data[i];
	}
	return sum;
}

/* ---------------------------------------------------------------------------
 * receive_packet()
 * Extended to handle the end-of-firmware sentinel:
 *   - Normal packet : [size > 0][data bytes][checksum]  → write to flash
 *   - End packet    : [size == 0]                       → finalize header
 *
 * When size == 0 is received the bootloader knows all firmware bytes have
 * been sent, computes the final CRC, and writes the validated header.
 * Only after this call will firmware_is_valid() return 1 on next boot.
 * --------------------------------------------------------------------------*/
uint8_t receive_packet(void)
{
    if (HAL_UART_Receive(&huart1, &size, 1, 100) != HAL_OK)
        return 0;

    /* End-of-firmware sentinel: size == 0 means "all done, finalize" */
    if (size == 0)
    {
        /* Write header with VALID state — device is now safe to reboot */
        flash_finalize_header();
        return 0;  /* return 0 so caller does not try to flash_write() */
    }

    if (size > 64) return 0;

    HAL_UART_Receive(&huart1, data, size, HAL_MAX_DELAY);
    HAL_UART_Receive(&huart1, &checksum, 1, HAL_MAX_DELAY);

    return (calc_checksum(data, size) == checksum);
}

void flash_erase(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase;
    uint32_t error;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_1;   // after bootloader
    erase.NbSectors = 7;             // erase rest
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASHEx_Erase(&erase, &error);

    HAL_FLASH_Lock();
}

void flash_write(uint8_t *data, uint8_t size)
{
    HAL_FLASH_Unlock();

    for (int i = 0; i < size; i += 4)
    {
    	if (flash_write_address >= FLASH_END)
    	    {
    	        Error_Handler();
    	    }
    	uint32_t word = 0xFFFFFFFF;

        // pack 4 bytes safely
        for (int j = 0; j < 4 && (i + j) < size; j++)
        {
            ((uint8_t*)&word)[j] = data[i + j];
        }

        //HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_write_address, word);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_write_address, word) != HAL_OK)
        {
            Error_Handler();  // catch flash errors
        }
        flash_write_address += 4;
    }

    HAL_FLASH_Lock();
//    if (flash_write_address >= FLASH_END)
//    {
//        Error_Handler();
//    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
