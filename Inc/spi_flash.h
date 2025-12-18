#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include <stdint.h>
#include <string.h>
#include "fm33xx.h"
#include "main.h"
#include "fm33lc0xx.h"
#include "fm33lc0xx_ll_spi.h"

#define LOG_MAGIC 0x35474F4Cu  // "LOG5" ? little-endian

typedef struct {
    uint32_t magic;
    char line[5][16];
} Log5Record;


// Flash ports
#define FLASH_CS_PORT     GPIOC
#define FLASH_CS_PIN      LL_GPIO_PIN_3
#define FLASH_MISO_PIN    LL_GPIO_PIN_9
#define FLASH_MOSI_PIN    LL_GPIO_PIN_10
#define FLASH_SCLK_PIN    LL_GPIO_PIN_8

// config Flash
#define FILE_ADDRESS      0x000000
#define FILE_NAME         "file.txt"
#define FILE_CONTENT      "Hello world!"
#define FILE_SIZE         (strlen(FILE_CONTENT) + 1)

// comands P25Q16SH
#define CMD_WRITE_ENABLE  0x06
#define CMD_WRITE_DISABLE 0x04
#define CMD_READ_STATUS   0x05
#define CMD_WRITE_STATUS  0x01
#define CMD_PAGE_PROGRAM  0x02
#define CMD_READ_DATA     0x03
#define CMD_SECTOR_ERASE  0x20
#define CMD_CHIP_ERASE    0xC7
#define CMD_POWER_DOWN    0xB9
#define CMD_RELEASE_POWER_DOWN 0xAB
#define CMD_READ_ID       0x9F

#define STATUS_BUSY       0x01

//struct for file
typedef struct {
    char filename[32];
    uint32_t size;
    uint32_t timestamp;
    char data[256];
} FileEntry;

/*
 * Your code
*/


/*
 * Function init in fm33lc0xx
*/
void FlashGpio_init(void);

void Flash_CS_Low(void);
void Flash_CS_High(void);

void FlashSpi_init(void);

uint8_t SPI_TransmitReceive(uint8_t data);
//alternative function for bigsize data
void SPI_Transmit(uint8_t *data, uint32_t size);
void SPI_Receive(uint8_t *buffer, uint32_t size);
//----------------------------------------------------

/*
 * Function for work with P25Q16SH
*/

uint8_t Flash_ReadStatus(void);
void Flash_WaitForReady(void);
void Flash_WriteEnable(void);
void Flash_SectorErase(uint32_t address);
void Flash_PageProgram(uint32_t address, uint8_t *data, uint16_t size);
void Flash_ReadData(uint32_t address, uint8_t *buffer, uint32_t size);
uint32_t Flash_ReadID(void);
uint8_t Flash_CheckID(void);

//----------------------------------------------------

uint8_t WriteFileToFlash(void);
/* Write arbitrary text into the single FileEntry at FILE_ADDRESS */
uint8_t Flash_WriteFileEntry(const char *filename, const uint8_t *data, uint32_t size);
/* Convenience: format and store temperature value in Q4 (°C * 16) */
uint8_t Flash_WriteTemperatureFile_Q4(int16_t temp_q4);
/* Convenience: format and store temperature plus time (HH:MM:SS) in one line */
uint8_t Flash_WriteTemperatureWithTimeFile_Q4(int16_t temp_q4, uint8_t hh, uint8_t mm, uint8_t ss);
/* Log ring: keep last 5 lines in the same file */
uint8_t Flash_LogLine_Ring5(const char *line, uint32_t size);
uint8_t Flash_LogTemperatureWithTime_Ring5_Q4(int16_t temp_q4, uint8_t hh, uint8_t mm, uint8_t ss);
static void Flash_WriteBytes(uint32_t addr, const uint8_t *data, uint32_t size);

#endif
