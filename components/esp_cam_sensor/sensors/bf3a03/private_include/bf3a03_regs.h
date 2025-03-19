/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * BF3A03 register definitions.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* bf3a03 registers */
#define BF3A03_REG_DELAY               0xfe

#define BF3A03_REG_SOFTWARE_STANDBY    0x09
#define BF3A03_REG_COM7                0X12
#define BF3A03_REG_CHIP_ID_H           0xfc
#define BF3A03_REG_CHIP_ID_L           0xfd
#define BF3A03_REG_TEST_MODE           0xb9

#ifdef __cplusplus
}
#endif
