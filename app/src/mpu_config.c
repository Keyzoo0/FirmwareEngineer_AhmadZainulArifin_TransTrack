/**
 * @file  mpu_config.c
 * @brief Memory Protection Unit layout (PRIVDEFENA: background map for anything not listed).
 *
 *  Region | Base        | Size  | Access            | Purpose
 *  -------+-------------+-------+-------------------+----------------------------------------
 *  0      | 0x0000_0000 | 1 KB  | no access         | NULL-pointer trap (boot alias of flash)
 *  1      | 0x0800_0000 | 32 KB | RO, executable    | bootloader cannot be overwritten by app
 *  2      | 0x2000_0000 | 128 KB| RW, execute-never | SRAM: no code injection via buffers
 *  3      | 0x1000_0000 | 64 KB | RW, execute-never | CCM: RTOS heap + task stacks
 *  4      | 0x4000_0000 | 512 MB| RW, XN, device    | peripherals, strongly ordered access
 *  5      | 0x1FFF_0000 | 32 KB | RO, XN            | system memory + OTP + option bytes
 *
 * Flash outside the bootloader keeps the default (RWX) attributes because the app has to
 * program the journal, log pages and the inactive slot. Violations raise MemManage,
 * which is logged by the fault handler (fault_handler.c) and then resets the MCU.
 */
#include "mpu_config.h"
#include "stm32f4xx_hal.h"

static void region(uint8_t n, uint32_t base, uint8_t size, uint8_t ap, uint8_t xn,
                   uint8_t tex, uint8_t c, uint8_t b, uint8_t s)
{
    MPU_Region_InitTypeDef r = {0};
    r.Enable = MPU_REGION_ENABLE;
    r.Number = n;
    r.BaseAddress = base;
    r.Size = size;
    r.SubRegionDisable = 0x00;
    r.TypeExtField = tex;
    r.AccessPermission = ap;
    r.DisableExec = xn;
    r.IsShareable = s;
    r.IsCacheable = c;
    r.IsBufferable = b;
    HAL_MPU_ConfigRegion(&r);
}

void mpu_config(void)
{
    HAL_MPU_Disable();
    region(0, 0x00000000UL, MPU_REGION_SIZE_1KB,   MPU_REGION_NO_ACCESS, MPU_INSTRUCTION_ACCESS_DISABLE,
           MPU_TEX_LEVEL0, 0, 0, 0);
    region(1, 0x08000000UL, MPU_REGION_SIZE_32KB,  MPU_REGION_PRIV_RO_URO, MPU_INSTRUCTION_ACCESS_ENABLE,
           MPU_TEX_LEVEL0, 1, 0, 0);
    region(2, 0x20000000UL, MPU_REGION_SIZE_128KB, MPU_REGION_FULL_ACCESS, MPU_INSTRUCTION_ACCESS_DISABLE,
           MPU_TEX_LEVEL0, 1, 1, 1);
    region(3, 0x10000000UL, MPU_REGION_SIZE_64KB,  MPU_REGION_FULL_ACCESS, MPU_INSTRUCTION_ACCESS_DISABLE,
           MPU_TEX_LEVEL0, 1, 1, 0);
    region(4, 0x40000000UL, MPU_REGION_SIZE_512MB, MPU_REGION_FULL_ACCESS, MPU_INSTRUCTION_ACCESS_DISABLE,
           MPU_TEX_LEVEL0, 0, 1, 1);   /* device memory */
    region(5, 0x1FFF0000UL, MPU_REGION_SIZE_32KB,  MPU_REGION_PRIV_RO_URO, MPU_INSTRUCTION_ACCESS_DISABLE,
           MPU_TEX_LEVEL0, 1, 0, 0);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
}
