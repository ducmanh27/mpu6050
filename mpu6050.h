#ifndef MPU6050_H
#define MPU6050_H

#include <linux/bitops.h>

#define MPU6050_WHO_AM_I_REG    0x75
#define MPU6050_WHO_AM_I_VALUE  0x68
#define MPU6050_PWR_MGMT_1_REG  0x6B

#define MPU6050_PWR_MGMT_1_DEVICE_RESET    BIT(7)
#define MPU6050_PWR_MGMT_1_SLEEP           BIT(6)
#define MPU6050_PWR_MGMT_1_CYCLE           BIT(5)
#define MPU6050_PWR_MGMT_1_TEMP_DIS        BIT(3)
#define MPU6050_PWR_MGMT_1_CLKSEL_PLL_X    BIT(0)

#endif