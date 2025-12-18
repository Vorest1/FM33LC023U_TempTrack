#include "main.h"

int main(void)
{
    MF_Clock_Init();
    MF_SystemClock_Config();
    MF_Config_Init();

    UserInit();

    RTC_SimpleInit_IfNeeded();

    WKUP_init();
    WKUP_USB_init();

    uint8_t usb_started = 0;
    while (1)
    {
        uint8_t usb  = LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_2);
        uint8_t butt = LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_15);

        if (usb)
        {
            if (!usb_started)
            {
                usb_started = 1;

                FlashGpio_init();
                FlashSpi_init();
                Flash_CS_High();

                MSC_PrepareImage();   /* reload file from external SPI flash into RAM image */
                USBInit();            /* enumerate MSC */
            }

            /* Indicate USB active */
            LED1_ON();
            LED0_OFF();
        }
        else if (butt)
        {
            /* Button press (WKUP0) while NOT connected to USB:
             * read NST112 temperature and store it into the single file in SPI flash.
             */
            FlashGpio_init();
            FlashSpi_init();
            Flash_CS_High(); // init all pins for SPI_FLASH P25Q16SH

            NST112_GPIO_Init(); // init nst112 sensor pins for Temperature

            int16_t t_q4 = 0;
            int rc = NST112_ReadTempQ4(&t_q4);
            if (rc == 0) {
                (void)RTC_SimpleInit_IfNeeded();
                unsigned char hh=0, mm=0, ss=0;
                RTC_ReadTimeHMS(&hh, &mm, &ss);
                (void)Flash_WriteTemperatureWithTimeFile_Q4(t_q4, hh, mm, ss);
                blink_green();
            } else {
                /* Error reading sensor: append diagnostics into the ring log */
                (void)RTC_SimpleInit_IfNeeded();
                unsigned char hh=0, mm=0, ss=0;
                RTC_ReadTimeHMS(&hh, &mm, &ss);

                char err[96];
                (void)sprintf(err, "%02u:%02u:%02u  NST112 error, rc=%d\r\n", hh, mm, ss, rc);
                (void)Flash_LogLine_Ring5(err, (uint32_t)strlen(err));

                blink_red();
                blink_red();
            }
            /* Wait for button release to avoid repeated writes while held */
            while (LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_15)) {
                DelayMs(10);
            }
        }
        else
        {
            usb_started = 0; /* allow MSC re-init on next insertion */
            LED0_OFF();
            LED1_OFF();

            Sleep_Deep();

            UserInit();
            RTC_SimpleInit_IfNeeded();
        }
    }
}
