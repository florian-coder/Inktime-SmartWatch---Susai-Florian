/*
 * nRF52840 startup: vector table, .data/.bss initialisation, FPU enable (in
 * SystemInit from the Nordic MDK, which also applies the silicon errata fixes).
 */
#include <stdint.h>

#include "nrf.h"

/* Linker-script symbols, declared as arrays: only their addresses are meaningful. */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

void Reset_Handler(void)
{
    const uintptr_t data_words = ((uintptr_t)_edata - (uintptr_t)_sdata) / 4u;
    for (uintptr_t i = 0; i < data_words; i++) {
        _sdata[i] = _sidata[i];
    }
    const uintptr_t bss_words = ((uintptr_t)_ebss - (uintptr_t)_sbss) / 4u;
    for (uintptr_t i = 0; i < bss_words; i++) {
        _sbss[i] = 0;
    }
    SystemInit();
    main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    /* Unexpected interrupt or fault: hang here, the watchdog will reset us. */
    for (;;) {
    }
}

#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void MemoryManagement_Handler(void) WEAK_ALIAS;
void BusFault_Handler(void) WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void DebugMon_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;
void SysTick_Handler(void) WEAK_ALIAS;

void POWER_CLOCK_IRQHandler(void) WEAK_ALIAS;
void RADIO_IRQHandler(void) WEAK_ALIAS;
void UARTE0_UART0_IRQHandler(void) WEAK_ALIAS;
void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler(void) WEAK_ALIAS;
void SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler(void) WEAK_ALIAS;
void NFCT_IRQHandler(void) WEAK_ALIAS;
void GPIOTE_IRQHandler(void) WEAK_ALIAS;
void SAADC_IRQHandler(void) WEAK_ALIAS;
void TIMER0_IRQHandler(void) WEAK_ALIAS;
void TIMER1_IRQHandler(void) WEAK_ALIAS;
void TIMER2_IRQHandler(void) WEAK_ALIAS;
void RTC0_IRQHandler(void) WEAK_ALIAS;
void TEMP_IRQHandler(void) WEAK_ALIAS;
void RNG_IRQHandler(void) WEAK_ALIAS;
void ECB_IRQHandler(void) WEAK_ALIAS;
void CCM_AAR_IRQHandler(void) WEAK_ALIAS;
void WDT_IRQHandler(void) WEAK_ALIAS;
void RTC1_IRQHandler(void) WEAK_ALIAS;
void QDEC_IRQHandler(void) WEAK_ALIAS;
void COMP_LPCOMP_IRQHandler(void) WEAK_ALIAS;
void SWI0_EGU0_IRQHandler(void) WEAK_ALIAS;
void SWI1_EGU1_IRQHandler(void) WEAK_ALIAS;
void SWI2_EGU2_IRQHandler(void) WEAK_ALIAS;
void SWI3_EGU3_IRQHandler(void) WEAK_ALIAS;
void SWI4_EGU4_IRQHandler(void) WEAK_ALIAS;
void SWI5_EGU5_IRQHandler(void) WEAK_ALIAS;
void TIMER3_IRQHandler(void) WEAK_ALIAS;
void TIMER4_IRQHandler(void) WEAK_ALIAS;
void PWM0_IRQHandler(void) WEAK_ALIAS;
void PDM_IRQHandler(void) WEAK_ALIAS;
void MWU_IRQHandler(void) WEAK_ALIAS;
void PWM1_IRQHandler(void) WEAK_ALIAS;
void PWM2_IRQHandler(void) WEAK_ALIAS;
void SPIM2_SPIS2_SPI2_IRQHandler(void) WEAK_ALIAS;
void RTC2_IRQHandler(void) WEAK_ALIAS;
void I2S_IRQHandler(void) WEAK_ALIAS;
void FPU_IRQHandler(void) WEAK_ALIAS;
void USBD_IRQHandler(void) WEAK_ALIAS;
void UARTE1_IRQHandler(void) WEAK_ALIAS;
void QSPI_IRQHandler(void) WEAK_ALIAS;
void CRYPTOCELL_IRQHandler(void) WEAK_ALIAS;
void PWM3_IRQHandler(void) WEAK_ALIAS;
void SPIM3_IRQHandler(void) WEAK_ALIAS;

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used)) const vector_t vector_table[16 + 48] = {
    (vector_t)_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemoryManagement_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0,
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    /* External interrupts, numbered as in nrf52840.h (IRQn_Type) */
    POWER_CLOCK_IRQHandler,                       /* 0 */
    RADIO_IRQHandler,                             /* 1 */
    UARTE0_UART0_IRQHandler,                      /* 2 */
    SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler, /* 3 */
    SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler, /* 4 */
    NFCT_IRQHandler,                              /* 5 */
    GPIOTE_IRQHandler,                            /* 6 */
    SAADC_IRQHandler,                             /* 7 */
    TIMER0_IRQHandler,                            /* 8 */
    TIMER1_IRQHandler,                            /* 9 */
    TIMER2_IRQHandler,                            /* 10 */
    RTC0_IRQHandler,                              /* 11 */
    TEMP_IRQHandler,                              /* 12 */
    RNG_IRQHandler,                               /* 13 */
    ECB_IRQHandler,                               /* 14 */
    CCM_AAR_IRQHandler,                           /* 15 */
    WDT_IRQHandler,                               /* 16 */
    RTC1_IRQHandler,                              /* 17 */
    QDEC_IRQHandler,                              /* 18 */
    COMP_LPCOMP_IRQHandler,                       /* 19 */
    SWI0_EGU0_IRQHandler,                         /* 20 */
    SWI1_EGU1_IRQHandler,                         /* 21 */
    SWI2_EGU2_IRQHandler,                         /* 22 */
    SWI3_EGU3_IRQHandler,                         /* 23 */
    SWI4_EGU4_IRQHandler,                         /* 24 */
    SWI5_EGU5_IRQHandler,                         /* 25 */
    TIMER3_IRQHandler,                            /* 26 */
    TIMER4_IRQHandler,                            /* 27 */
    PWM0_IRQHandler,                              /* 28 */
    PDM_IRQHandler,                               /* 29 */
    0,                                            /* 30 */
    0,                                            /* 31 */
    MWU_IRQHandler,                               /* 32 */
    PWM1_IRQHandler,                              /* 33 */
    PWM2_IRQHandler,                              /* 34 */
    SPIM2_SPIS2_SPI2_IRQHandler,                  /* 35 */
    RTC2_IRQHandler,                              /* 36 */
    I2S_IRQHandler,                               /* 37 */
    FPU_IRQHandler,                               /* 38 */
    USBD_IRQHandler,                              /* 39 */
    UARTE1_IRQHandler,                            /* 40 */
    QSPI_IRQHandler,                              /* 41 */
    CRYPTOCELL_IRQHandler,                        /* 42 */
    0,                                            /* 43 */
    0,                                            /* 44 */
    PWM3_IRQHandler,                              /* 45 */
    0,                                            /* 46 */
    SPIM3_IRQHandler,                             /* 47 */
};
