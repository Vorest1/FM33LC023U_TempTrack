#include "wkup.h"
#include "user_init.h"   
#include "fm33lc0xx.h"   

void NMI_Handler(void)
{
    if(SET == LL_PMU_IsActiveFlag_WakeupPIN(PMU, LL_PMU_WKUP2PIN))
    {
				LL_PMU_ClearFlag_WakeupPIN(PMU, LL_PMU_WKUP2PIN);
    }
		if(SET == LL_PMU_IsActiveFlag_WakeupPIN(PMU, LL_PMU_WKUP0PIN))
    {
				LL_PMU_ClearFlag_WakeupPIN(PMU, LL_PMU_WKUP0PIN);
    }
}

void WKUP_init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    LL_RCC_Enable_SleepmodeExternalInterrupt();
    LL_RCC_Group1_EnableOperationClock(LL_RCC_OPERATION1_CLOCK_EXTI);

    GPIO_InitStruct.Pin        = LL_GPIO_PIN_15;
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_INPUT;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = DISABLE;
    GPIO_InitStruct.RemapPin   = DISABLE;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    LL_GPIO_SetWkupEntry(GPIO_COMMON, LL_GPIO_WKUP_INT_ENTRY_NMI);
    LL_GPIO_SetWkupPolarity(GPIO_COMMON, LL_GPIO_WKUP_0, LL_GPIO_WKUP_POLARITY_RISING);
    LL_GPIO_EnableWkup(GPIO_COMMON, LL_GPIO_WKUP_0);
}

void WKUP_USB_init(void)
{
		LL_GPIO_InitTypeDef gpio_init;
		LL_RCC_Enable_SleepmodeExternalInterrupt();
    LL_RCC_Group1_EnableOperationClock(LL_RCC_OPERATION1_CLOCK_EXTI);
		
		gpio_init.Pin        = LL_GPIO_PIN_2;
		gpio_init.Mode       = LL_GPIO_MODE_INPUT;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull       = DISABLE; //en
    gpio_init.RemapPin   = DISABLE;
    LL_GPIO_Init(GPIOB, &gpio_init);
	
		LL_GPIO_SetWkupEntry(GPIO_COMMON, LL_GPIO_WKUP_INT_ENTRY_NMI);
		LL_GPIO_SetWkupPolarity(GPIO_COMMON, LL_GPIO_WKUP_2, LL_GPIO_WKUP_POLARITY_RISING);
		LL_GPIO_EnableWkup(GPIO_COMMON, LL_GPIO_WKUP_2);
}

void Sleep_Deep(void)
{
    // ?????: ?? ???????????? ARM SLEEPDEEP ??? ????? ? Sleep/DeepSleep ?? FM33
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk; // ????? ?? ?????? SLPEIF :contentReference[oaicite:4]{index=4}

    // ???????? SysTick, ????? ?? ???? “??????” ?? ????????? ????? ???????????
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

    // ???????? ??????? ????? ??????????? (WKFSEL): 00=8MHz, 01=16MHz, 10=24MHz :contentReference[oaicite:5]{index=5}
    // ?????? ??, ??????? ????????????? ?????? UserInit() (RCHF_CLOCK).
    uint32_t cr = PMU->CR;
    cr &= ~((3u << 0) | (1u << 9) | (3u << 10));  // PMOD[1:0], SLPDP, WKFSEL[11:10]
    cr |=  (2u << 0);    // PMOD=10 => Sleep/DeepSleep 
    cr |=  (1u << 9);    // SLPDP=1 => DeepSleep 
    cr |=  (0u << 10);   // WKFSEL=00 => wake @ RCHF 8MHz :contentReference[oaicite:8]{index=8}
    PMU->CR = cr;

    __DSB();
    __WFI(); 
    __ISB();
}

