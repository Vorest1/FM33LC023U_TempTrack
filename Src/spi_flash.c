#include "spi_flash.h"
#include <stdio.h>

/*
 * Your code
*/

void FlashGpio_init(void)
{
    LL_GPIO_InitTypeDef io;

    // CS 
    io.Pin        = FLASH_CS_PIN;
    io.Mode       = LL_GPIO_MODE_OUTPUT;
    io.Pull       = DISABLE;
    io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    io.RemapPin   = DISABLE;
    LL_GPIO_Init(FLASH_CS_PORT, &io);
    Flash_CS_High();

    // SCK (PC8)
    io.Pin        = FLASH_SCLK_PIN;
    io.Mode       = LL_GPIO_MODE_DIGITAL;
    io.Pull       = DISABLE;
    io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    io.RemapPin   = ENABLE;
    LL_GPIO_Init(FLASH_CS_PORT, &io);

    // MISO (PC9)  <-- DIGITAL, INPUT
    io.Pin      = FLASH_MISO_PIN;
    io.Mode     = LL_GPIO_MODE_DIGITAL;
    io.Pull     = DISABLE;
    io.RemapPin = ENABLE;
    LL_GPIO_Init(FLASH_CS_PORT, &io);

    // MOSI (PC10) <-- DIGITAL, OUTPUT
    io.Pin        = FLASH_MOSI_PIN;
    io.Mode       = LL_GPIO_MODE_DIGITAL;
    io.Pull       = DISABLE;
    io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    io.RemapPin   = ENABLE;
    LL_GPIO_Init(FLASH_CS_PORT, &io);
}


void Flash_CS_Low(void) {
    LL_GPIO_ResetOutputPin(FLASH_CS_PORT, FLASH_CS_PIN);
}

void Flash_CS_High(void) {
    LL_GPIO_SetOutputPin(FLASH_CS_PORT, FLASH_CS_PIN);
}

void FlashSpi_init(void)
{
		LL_SPI_InitTypeDef SPI_InitStruct;
	
		LL_SPI_DeInit(SPI2);
	
		LL_SPI_StructInit(&SPI_InitStruct);
	
		// cofig SPI for P25Q16SH (Mode 0: CPOL=0, CPHA=0)
    SPI_InitStruct.Mode = LL_SPI_WORK_MODE_MASTER;          // Master mode
    SPI_InitStruct.TransferMode = LL_SPI_MODE_FULL_DUPLEX; // 
    SPI_InitStruct.DataWidth = LL_SPI_DATAWIDTH_8BIT;        // 8-bit sending
    SPI_InitStruct.ClockPolarity = LL_SPI_SPI_POLARITY_LOW;  // CPOL = 0
    SPI_InitStruct.ClockPhase = LL_SPI_SPI_PHASE_1EDGE;         // CPHA = 0 
    SPI_InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV8;
    SPI_InitStruct.BitOrder = LL_SPI_BIT_ORDER_MSB_FIRST;
    
    SPI_InitStruct.SSN = 0; // enable CS
    
    // init SPI
    LL_SPI_Init(SPI2, &SPI_InitStruct);
	
		LL_SPI_ResetSSNPin(SPI2);
		LL_SPI_SetSSNPin(SPI2);
		//LL_SPI_SetSSNPin(SPI2, LL_SPI_SSN_SEND_MODE_HIGH);
	
		///enable spi
		LL_SPI_Enable(SPI2);
    
    // clear buffers
    LL_SPI_TxBuffClear(SPI2);
    LL_SPI_RxBuffClear(SPI2);
}

uint8_t SPI_TransmitReceive(uint8_t data)
{
		// wait , while TX buffer is not free
		while(!LL_SPI_IsActiveFlag_TxBufffEmpty(SPI2));
		//write in TX buffer
		LL_SPI_WriteTxBuff(SPI2, data);
	
		while(!LL_SPI_IsActiveFlag_RxBufffFull(SPI2));
	
		return (uint8_t)LL_SPI_ReadRxBuff(SPI2);
}

void SPI_Transmit(uint8_t *data, uint32_t size) {
    for(uint32_t i = 0; i < size; i++) {
        SPI_TransmitReceive(data[i]);
    }
}

void SPI_Receive(uint8_t *buffer, uint32_t size) {
    for(uint32_t i = 0; i < size; i++) {
        buffer[i] = SPI_TransmitReceive(0xFF);
    }
}

//------------------------------------------------------------

uint8_t Flash_ReadStatus(void)
{
		uint8_t status;
		Flash_CS_Low();
		SPI_TransmitReceive(CMD_READ_STATUS);
		status = SPI_TransmitReceive(0x00);
		Flash_CS_High();
		return status;
}

void Flash_WaitForReady(void) {
    while(Flash_ReadStatus() & STATUS_BUSY) {
        // delay between calls
        for(volatile int i = 0; i < 100; i++);
    }
}

void Flash_WriteEnable(void) {
    Flash_CS_Low();
    SPI_TransmitReceive(CMD_WRITE_ENABLE);
    Flash_CS_High();
}

void Flash_SectorErase(uint32_t address) {
    // Write enable
    Flash_WriteEnable();
    Flash_WaitForReady();
    
    // Send comand for erase sector
    Flash_CS_Low();
    SPI_TransmitReceive(CMD_SECTOR_ERASE);
    SPI_TransmitReceive((address >> 16) & 0xFF);  // addr byte 2
    SPI_TransmitReceive((address >> 8) & 0xFF);   // addr byte 1
    SPI_TransmitReceive(address & 0xFF);          // addr byte 0
    Flash_CS_High();
    
    // wait erase ending
    Flash_WaitForReady();
}

void Flash_PageProgram(uint32_t address, uint8_t *data, uint16_t size) {
    // maximum page size for P25Q16SH - 256 byte
    if(size > 256) {
        size = 256;
    }
    
    // write enable
    Flash_WriteEnable();
    Flash_WaitForReady();
    
    // send comand for programming page
    Flash_CS_Low();
    SPI_TransmitReceive(CMD_PAGE_PROGRAM);
    SPI_TransmitReceive((address >> 16) & 0xFF);
    SPI_TransmitReceive((address >> 8) & 0xFF);
    SPI_TransmitReceive(address & 0xFF);
    
    // send data
    for(uint16_t i = 0; i < size; i++) {
        SPI_TransmitReceive(data[i]);
    }
    
    Flash_CS_High();
    
    // wait end programming
    Flash_WaitForReady();
}

void Flash_ReadData(uint32_t address, uint8_t *buffer, uint32_t size) {
    Flash_CS_Low();
    SPI_TransmitReceive(CMD_READ_DATA);
    SPI_TransmitReceive((address >> 16) & 0xFF);
    SPI_TransmitReceive((address >> 8) & 0xFF);
    SPI_TransmitReceive(address & 0xFF);
    
    // read data
    for(uint32_t i = 0; i < size; i++) {
        buffer[i] = SPI_TransmitReceive(0xFF);
    }
    
    Flash_CS_High();
}

uint32_t Flash_ReadID(void) {
    uint32_t id = 0;
    
    Flash_CS_Low();
    SPI_TransmitReceive(CMD_READ_ID);
    
    // P25Q16SH return 3 byte ID
    id = SPI_TransmitReceive(0x00) << 16;  // Manufacturer ID
    id |= SPI_TransmitReceive(0x00) << 8;  // Memory type
    id |= SPI_TransmitReceive(0x00);       // Capacity
    
    Flash_CS_High();
    return id;
}

// check Flash ID (for P25Q16SH expected 0x856015)
uint8_t Flash_CheckID(void) {
    uint32_t id = Flash_ReadID();
    
    // check ID P25Q16SH
    if((id >> 16) == 0x85) {  // Manufacturer ID (Puya)
        return 1;
    }
    return 0;
}

uint8_t WriteFileToFlash(void)
{
    FileEntry file;
    memset(&file, 0, sizeof(file));

    strncpy(file.filename, FILE_NAME, sizeof(file.filename) - 1);
    file.size = (uint32_t)strlen(FILE_CONTENT);   // '\0'
    file.timestamp = 0;

    memset(file.data, 0, sizeof(file.data));
    memcpy(file.data, FILE_CONTENT, file.size);

    if (!Flash_CheckID()) return 0;

    Flash_SectorErase(FILE_ADDRESS);
    Flash_WriteBytes(FILE_ADDRESS, (uint8_t*)&file, (uint32_t)sizeof(file));

    return 2;
}

uint8_t Flash_WriteFileEntry(const char *filename, const uint8_t *data, uint32_t size)
{
    if (!Flash_CheckID()) return 0;

    FileEntry fe;
    memset(&fe, 0, sizeof(fe));

    if (filename && filename[0] != '\0') {
        strncpy(fe.filename, filename, sizeof(fe.filename) - 1);
    } else {
        strncpy(fe.filename, FILE_NAME, sizeof(fe.filename) - 1);
    }

    if (size > sizeof(fe.data)) size = sizeof(fe.data);
    fe.size = size;
    fe.timestamp = 0;

    if (data && size) {
        memcpy(fe.data, data, size);
    }

    Flash_SectorErase(FILE_ADDRESS);
    Flash_WriteBytes(FILE_ADDRESS, (const uint8_t*)&fe, (uint32_t)sizeof(fe));
    return 1;
}

/* -------- Ring log helpers: keep last 5 button presses -------- */

#define LOG_RING_LINES 5u

static int fileentry_valid_local(const FileEntry *fe)
{
    if (!fe) return 0;
    if (fe->size == 0u || fe->size > sizeof(fe->data) || fe->size == 0xFFFFFFFFu) return 0;
    if ((uint8_t)fe->filename[0] == 0xFFu || fe->filename[0] == '\0') return 0;
    if ((uint8_t)fe->data[0] == 0xFFu) return 0;
    return 1;
}

uint8_t Flash_LogLine_Ring5(const char *line, uint32_t size)
{
    if (!Flash_CheckID()) return 0;
    if (!line || size == 0u) return 0;

    if (size > sizeof(((FileEntry*)0)->data)) size = sizeof(((FileEntry*)0)->data);

    FileEntry fe_old;
    memset(&fe_old, 0, sizeof(fe_old));
    Flash_ReadData(FILE_ADDRESS, (uint8_t*)&fe_old, (uint32_t)sizeof(fe_old));

    const uint8_t *old = NULL;
    uint32_t old_len = 0u;
    if (fileentry_valid_local(&fe_old)) {
        old = (const uint8_t*)fe_old.data;
        old_len = fe_old.size;
        if (old_len > sizeof(fe_old.data)) old_len = sizeof(fe_old.data);
    }

    uint32_t starts[LOG_RING_LINES];
    uint32_t lens[LOG_RING_LINES];
    uint32_t nlines = 0u;

    if (old && old_len) {
        uint32_t line_start = 0u;
        for (uint32_t i = 0u; i < old_len && nlines < LOG_RING_LINES; i++) {
            if (old[i] == (uint8_t)'\n') {
                starts[nlines] = line_start;
                lens[nlines]   = (i + 1u) - line_start;
                nlines++;
                line_start = i + 1u;
            }
        }
        if (line_start < old_len && nlines < LOG_RING_LINES) {
            starts[nlines] = line_start;
            lens[nlines]   = old_len - line_start;
            nlines++;
        }
    }

    uint8_t out[256];
    memset(out, 0, sizeof(out));
    uint32_t out_len = 0u;

    uint32_t copy_new = size;
    if (copy_new > sizeof(out)) copy_new = sizeof(out);
    memcpy(out, line, copy_new);
    out_len = copy_new;

    /* Append up to 4 previous lines (newest-first already in file). */
    for (uint32_t i = 0u; i < nlines && i < (LOG_RING_LINES - 1u); i++) {
        if (out_len + lens[i] > sizeof(out)) break;
        memcpy(&out[out_len], &old[starts[i]], lens[i]);
        out_len += lens[i];
    }

    return Flash_WriteFileEntry(FILE_NAME, out, out_len);
}

uint8_t Flash_LogTemperatureWithTime_Ring5_Q4(int16_t temp_q4, uint8_t hh, uint8_t mm, uint8_t ss)
{
    char buf[96];

    int sign = (temp_q4 < 0);
    int16_t a = (int16_t)(sign ? -temp_q4 : temp_q4);
    int whole = (int)(a >> 4);
    int frac_q4 = (int)(a & 0x0F);
    int frac_4d = frac_q4 * 625; /* 0..9375 */

    if (sign) {
        (void)sprintf(buf, "%02u:%02u:%02u  Temperature: -%d.%04d C\r\n", hh, mm, ss, whole, frac_4d);
    } else {
        (void)sprintf(buf, "%02u:%02u:%02u  Temperature: %d.%04d C\r\n", hh, mm, ss, whole, frac_4d);
    }

    return Flash_LogLine_Ring5(buf, (uint32_t)strlen(buf));
}

uint8_t Flash_WriteTemperatureFile_Q4(int16_t temp_q4)
{
    /* Convert Q4 (°C*16) to a human-readable string with 4 decimals (1/16=0.0625 => 4 decimals via *625). */
    char buf[64];

    int sign = (temp_q4 < 0);
    int16_t a = (int16_t)(sign ? -temp_q4 : temp_q4);
    int whole = (int)(a >> 4);
    int frac_q4 = (int)(a & 0x0F);
    int frac_4d = frac_q4 * 625; /* 0..9375 */

    if (sign) {
        (void)sprintf(buf, "Temperature: -%d.%04d C\r\n", whole, frac_4d);
    } else {
        (void)sprintf(buf, "Temperature: %d.%04d C\r\n", whole, frac_4d);
    }

    return Flash_WriteFileEntry(FILE_NAME, (const uint8_t*)buf, (uint32_t)strlen(buf));
}

uint8_t Flash_WriteTemperatureWithTimeFile_Q4(int16_t temp_q4, uint8_t hh, uint8_t mm, uint8_t ss)
{
    /* Backward-compatible wrapper: now uses the 5-line ring log. */
    return Flash_LogTemperatureWithTime_Ring5_Q4(temp_q4, hh, mm, ss);
}



static void Flash_WriteBytes(uint32_t addr, const uint8_t *data, uint32_t size)
{
    while (size) {
        uint32_t page_off = addr & 0xFFu;
        uint32_t space = 256u - page_off;
        uint16_t chunk = (uint16_t)((size < space) ? size : space);

        Flash_PageProgram(addr, (uint8_t*)data, chunk); // WriteEnable/Wait
        addr += chunk;
        data += chunk;
        size -= chunk;
    }
}
