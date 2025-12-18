#include "msc_mem.h"
#include "spi_flash.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

/*
 * MSC: read-only FAT12 volume in RAM.
 * File content is cached from external SPI flash (FileEntry at FILE_ADDRESS)
 * by calling MSC_PrepareImage() BEFORE USBInit().
 *
 * Exposes exactly 1 file (name from flash if valid, else FILE_NAME),
 * with content "Hello World!" (enforced for now).
 */

#define STORAGE_LUN_NBR          1

/* FAT12 tiny layout */
#define SECTOR_SIZE             512u
#define VOL_SECTORS             64u   /* 32KB virtual disk */

#define BPB_SEC_PER_CLUS        1u
#define BPB_RSVD_SEC_CNT        1u
#define BPB_NUM_FATS            2u
#define BPB_ROOT_ENT_CNT        16u   /* 16 entries => 1 sector */
#define BPB_FATSZ16             1u

#define ROOT_DIR_SECTORS        1u

#define LBA_BOOT                0u
#define LBA_FAT1                (LBA_BOOT + BPB_RSVD_SEC_CNT)                 /* 1 */
#define LBA_FAT2                (LBA_FAT1 + BPB_FATSZ16)                      /* 2 */
#define LBA_ROOT                (LBA_FAT2 + BPB_FATSZ16)                      /* 3 */
#define LBA_DATA                (LBA_ROOT + ROOT_DIR_SECTORS)                 /* 4 */
#define CLUSTER2_LBA            (LBA_DATA)                                    /* cluster #2 => first data sector */

/* "Hello World!" source-of-truth for now */
#define MSC_FILE_CONTENT        "Hello 2 World!"

static volatile uint8_t g_storage_ready = 0u;
static uint8_t g_prepared = 0u;

/* Cached file (served to host) */
static uint8_t  g_file_buf[256];
static uint32_t g_file_len = 0u;
static char     g_file_name[32];   /* from flash if valid */

static int8_t STORAGE_Inquirydata[] = {
    0x00, 0x80, 0x02, 0x02,
    (USBD_STD_INQUIRY_LENGTH - 5),
    0x00, 0x00, 0x00,
    'F','M','3','3',' ',' ',' ',' ',
    'S','P','I',' ','F','l','a','s','h',' ','M','S','C',' ',' ',' ',
    '1','.','0','0'
};

static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); }
static void le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void make_83_name(const char *in, char out83[11])
{
    for (int i = 0; i < 11; i++) out83[i] = ' ';

    const char *dot = NULL;
    for (const char *p = in; *p; p++) { if (*p == '.') { dot = p; break; } }

    int nlen = dot ? (int)(dot - in) : (int)strlen(in);
    int elen = dot ? (int)strlen(dot + 1) : 0;

    if (nlen > 8) nlen = 8;
    if (elen > 3) elen = 3;

    for (int i = 0; i < nlen; i++) out83[i] = (char)toupper((unsigned char)in[i]);
    if (dot) {
        for (int i = 0; i < elen; i++) out83[8 + i] = (char)toupper((unsigned char)dot[1 + i]);
    }
}

/* ---------------- FAT12 sector builders ---------------- */

static void build_boot_sector(uint8_t *sec)
{
    memset(sec, 0, SECTOR_SIZE);

    sec[0] = 0xEB; sec[1] = 0x3C; sec[2] = 0x90;                 /* JMP */
    memcpy(&sec[3], "MSDOS5.0", 8);

    le16(&sec[11], SECTOR_SIZE);                                  /* BytsPerSec */
    sec[13] = (uint8_t)BPB_SEC_PER_CLUS;                          /* SecPerClus */
    le16(&sec[14], BPB_RSVD_SEC_CNT);                             /* RsvdSecCnt */
    sec[16] = (uint8_t)BPB_NUM_FATS;                              /* NumFATs */
    le16(&sec[17], BPB_ROOT_ENT_CNT);                             /* RootEntCnt */
    le16(&sec[19], (uint16_t)VOL_SECTORS);                        /* TotSec16 */
    sec[21] = 0xF8;                                               /* Media */
    le16(&sec[22], BPB_FATSZ16);                                  /* FATSz16 */

    le16(&sec[24], 63);                                           /* SecPerTrk */
    le16(&sec[26], 255);                                          /* NumHeads */
    le32(&sec[28], 0);                                            /* HiddSec */
    le32(&sec[32], 0);                                            /* TotSec32 */

    sec[36] = 0x80;                                               /* DrvNum */
    sec[38] = 0x29;                                               /* BootSig */
    le32(&sec[39], 0x20251217u);                                  /* VolID (change if needed) */
    memcpy(&sec[43], "FM33FLASH   ", 11);                         /* VolLab */
    memcpy(&sec[54], "FAT12   ", 8);                              /* FilSysType */

    sec[510] = 0x55; sec[511] = 0xAA;
}

static void build_fat_sector(uint8_t *sec)
{
    memset(sec, 0, SECTOR_SIZE);

    /* FAT12: cluster0/1 reserved */
    sec[0] = 0xF8;
    sec[1] = 0xFF;
    sec[2] = 0xFF;

    /* Mark cluster #2 as EOC (0xFFF).
       FAT12 offset = (clus * 3) / 2 = 3 for clus=2 (even). */
    sec[3] = 0xFF;        /* low 8 bits */
    sec[4] = 0x0F;        /* high 4 bits = 0xF, upper nibble belongs to cluster3 low nibble (keep 0) */
    sec[5] = 0x00;
}

static void build_root_dir_sector(uint8_t *sec)
{
    memset(sec, 0, SECTOR_SIZE);

    /* Entry 0: Volume label */
    memcpy(&sec[0], "FM33FLASH   ", 11);
    sec[11] = 0x08;

    /* Entry 1: file */
    char n83[11];
    const char *name_src = (g_file_name[0] != '\0') ? g_file_name : FILE_NAME;
    make_83_name(name_src, n83);
    memcpy(&sec[32], n83, 11);
    sec[32 + 11] = 0x20; /* ATTR_ARCHIVE */

    le16(&sec[32 + 26], 2);               /* First cluster */
    le32(&sec[32 + 28], g_file_len);      /* File size */
}

static void build_data_sector_cluster2(uint8_t *sec)
{
    memset(sec, 0, SECTOR_SIZE);

    if (g_file_len > 0u) {
        uint32_t n = (g_file_len > SECTOR_SIZE) ? SECTOR_SIZE : g_file_len;
        memcpy(sec, g_file_buf, n);
    }
}

static void build_sector(uint32_t lba, uint8_t *sec)
{
    if (lba == LBA_BOOT) {
        build_boot_sector(sec);
    } else if (lba == LBA_FAT1 || lba == LBA_FAT2) {
        build_fat_sector(sec);
    } else if (lba == LBA_ROOT) {
        build_root_dir_sector(sec);
    } else if (lba == CLUSTER2_LBA) {
        build_data_sector_cluster2(sec);
    } else {
        memset(sec, 0, SECTOR_SIZE);
    }
}

/* ---------------- External flash helpers (ONLY in MSC_PrepareImage) ---------------- */

static void set_default_file_to_ram(void)
{
    memset(g_file_buf, 0, sizeof(g_file_buf));
    memcpy(g_file_buf, MSC_FILE_CONTENT, sizeof(MSC_FILE_CONTENT) - 1u);
    g_file_len = (uint32_t)(sizeof(MSC_FILE_CONTENT) - 1u);

    memset(g_file_name, 0, sizeof(g_file_name));
    strncpy(g_file_name, FILE_NAME, sizeof(g_file_name) - 1u);
}

static int fileentry_valid(const FileEntry *fe)
{
    if (fe->size == 0u || fe->size > sizeof(fe->data) || fe->size == 0xFFFFFFFFu) return 0;
    if ((uint8_t)fe->filename[0] == 0xFFu || fe->filename[0] == '\0') return 0;
    /* If fully erased, first bytes usually 0xFF */
    if ((uint8_t)fe->data[0] == 0xFFu) return 0;
    return 1;
}

static void load_file_from_flash_to_ram(void)
{
			//start
	    set_default_file_to_ram();

    uint32_t id = Flash_ReadID();   // <-- ?????? ??????

    if (id == 0x000000u || id == 0xFFFFFFu) {
        memset(g_file_buf, 0, sizeof(g_file_buf));
        g_file_len = (uint32_t)sprintf((char*)g_file_buf, "SPI NO RESP, JEDEC=0x%06lX\r\n", id);

        memset(g_file_name, 0, sizeof(g_file_name));
        strncpy(g_file_name, "NOFLASH.TXT", sizeof(g_file_name) - 1u);
        return;
    }

    // ?????? ???????? -> ??????? ID, ???? CheckID ?? ??????????
    if (!Flash_CheckID()) {
        memset(g_file_buf, 0, sizeof(g_file_buf));
        g_file_len = (uint32_t)sprintf((char*)g_file_buf, "JEDEC=0x%06lX (update Flash_CheckID)\r\n", id);

        memset(g_file_name, 0, sizeof(g_file_name));
        strncpy(g_file_name, "JEDEC.TXT", sizeof(g_file_name) - 1u);
        return;
    }//end
		
    FileEntry fe;

    // 1) ???????? flash
    /*if (!Flash_CheckID()) {
        memset(g_file_buf, 0, sizeof(g_file_buf));
        memcpy(g_file_buf, "ERR: Flash_CheckID failed", 25);
        g_file_len = 25;
        memset(g_file_name, 0, sizeof(g_file_name));
        strncpy(g_file_name, "NOFLASH.TXT", sizeof(g_file_name) - 1);
        return;
    }*/

    // 2) ?????? ????????? ?????
    Flash_ReadData(FILE_ADDRESS, (uint8_t*)&fe, (uint32_t)sizeof(fe));

    if (!fileentry_valid(&fe)) {
        memset(g_file_buf, 0, sizeof(g_file_buf));
        memcpy(g_file_buf, "ERR: Bad FileEntry", 18);
        g_file_len = 18;
        memset(g_file_name, 0, sizeof(g_file_name));
        strncpy(g_file_name, "BADFILE.TXT", sizeof(g_file_name) - 1);
        return;
    }

    // 3) ??????????? ? RAM
    uint32_t n = fe.size;
    if (n > sizeof(g_file_buf)) n = sizeof(g_file_buf);
    memcpy(g_file_buf, fe.data, n);
    g_file_len = n;

    memset(g_file_name, 0, sizeof(g_file_name));
    strncpy(g_file_name, fe.filename, sizeof(g_file_name) - 1);
}

/* ---------------- USBD STORAGE callbacks ---------------- */

static int8_t STORAGE_Init(uint8_t lun);
static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint32_t *block_size);
static int8_t STORAGE_IsReady(uint8_t lun);
static int8_t STORAGE_IsWriteProtected(uint8_t lun);
static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_GetMaxLun(void);

static USBD_STORAGE_cb_TypeDef USBD_SPI_FLASH_fops = {
    STORAGE_Init,
    STORAGE_GetCapacity,
    STORAGE_IsReady,
    STORAGE_IsWriteProtected,
    STORAGE_Read,
    STORAGE_Write,
    STORAGE_GetMaxLun,
    (int8_t*)STORAGE_Inquirydata
};

USBD_STORAGE_cb_TypeDef *USBD_STORAGE_fops = &USBD_SPI_FLASH_fops;

static int8_t STORAGE_Init(uint8_t lun)
{
    (void)lun;
    /* Must be FAST: no SPI here */
    g_storage_ready = 1u;
    return 0;
}

static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
    (void)lun;
    if (!block_num || !block_size) return -1;

    *block_size = SECTOR_SIZE;
    *block_num  = VOL_SECTORS;
    return 0;
}

static int8_t STORAGE_IsReady(uint8_t lun)
{
    (void)lun;
    return g_storage_ready ? 0 : -1;
}

static int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
    (void)lun;
    return 1; /* READ ONLY: Windows won't try to create system files */
}

static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    if (!g_storage_ready || (buf == NULL)) return -1;
    if ((blk_addr + blk_len) > VOL_SECTORS) return -1;

    for (uint16_t i = 0; i < blk_len; i++) {
        build_sector(blk_addr + i, buf + ((uint32_t)i * SECTOR_SIZE));
    }
    return 0;
}

static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun; (void)buf; (void)blk_addr; (void)blk_len;
    return -1; /* should not be called when write-protected */
}

static int8_t STORAGE_GetMaxLun(void)
{
    return (STORAGE_LUN_NBR - 1);
}

/* Call this from main() BEFORE USBInit() */
void MSC_PrepareImage(void)
{
    /*
     * IMPORTANT:
     * Do NOT cache forever.
     * We must reload the file from SPI flash each time BEFORE USBInit(),
     * otherwise the host will always see the first cached content.
     */
    load_file_from_flash_to_ram();
    g_prepared = 1u;
}
