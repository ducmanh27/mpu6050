#ifndef MPU6050_H
#define MPU6050_H

#include <linux/bitops.h>

#define MPU6050_SMPRT_DIV_REG           0x19
#define MPU6050_CONFIG_REG              0x1A
#define MPU6050_GYRO_CONFIG_REG         0x1B
#define MPU6050_ACCEL_CONFIG_REG        0x1C
#define MPU6050_WHO_AM_I_REG            0x75
#define MPU6050_WHO_AM_I_VALUE          0x68
#define MPU6050_PWR_MGMT_1_REG          0x6B
#define MPU6050_INT_PIN_CFG_REG         0x37
#define MPU6050_INT_ENABLE_REG          0x38
#define MPU6050_ACCEL_XOUT_H_REG        0x3B
#define MPU6050_ACCEL_XOUT_L_REG        0x3C
#define MPU6050_ACCEL_YOUT_H_REG        0x3D
#define MPU6050_ACCEL_YOUT_L_REG        0x3E
#define MPU6050_ACCEL_ZOUT_H_REG        0x3F
#define MPU6050_ACCEL_ZOUT_L_REG        0x40
#define MPU6050_TEMP_OUT_H_REG          0x41
#define MPU6050_TEMP_OUT_L_REG          0x42
#define MPU6050_GYRO_XOUT_H_REG         0x43
#define MPU6050_GYRO_XOUT_L_REG         0x44
#define MPU6050_GYRO_YOUT_H_REG         0x45
#define MPU6050_GYRO_YOUT_L_REG         0x46
#define MPU6050_GYRO_ZOUT_H_REG         0x47
#define MPU6050_GYRO_ZOUT_L_REG         0x48
#define MPU6050_DATA_START_REG    0x3B
#define MPU6050_DATA_LEN          14

#define MPU6050_GYRO_CONFIG_FS_SEL_250      (0 << 3)
#define MPU6050_GYRO_CONFIG_FS_SEL_500      (1 << 3)
#define MPU6050_GYRO_CONFIG_FS_SEL_1000     (2 << 3)
#define MPU6050_GYRO_CONFIG_FS_SEL_2000     (3 << 3)

#define MPU6050_ACCEL_CONFIG_AFS_SEL_2      (0 << 3)
#define MPU6050_ACCEL_CONFIG_AFS_SEL_4      (1 << 3)
#define MPU6050_ACCEL_CONFIG_AFS_SEL_8      (2 << 3)
#define MPU6050_ACCEL_CONFIG_AFS_SEL_16     (3 << 3)
#define MPU6050_PWR_MGMT_1_DEVICE_RESET     BIT(7)
#define MPU6050_PWR_MGMT_1_SLEEP            BIT(6)
#define MPU6050_PWR_MGMT_1_CYCLE            BIT(5)
#define MPU6050_PWR_MGMT_1_TEMP_DIS         BIT(3)
#define MPU6050_PWR_MGMT_1_CLKSEL_PLL_X     BIT(0)

#define MPU6050_GYRO_CONFIG_FS_SEL_MASK    (BIT(4) | BIT(3))
#define MPU6050_ACCEL_CONFIG_AFS_SEL_MASK  (BIT(4) | BIT(3))
#define MPU6050_SLEEP_CONFIG_MASK           (BIT(6))
#define MPU6050_CLKSEL_MASK           (BIT(2) | BIT(1) | BIT(0))

/* INT_PIN_CFG bits */
#define MPU6050_INT_LEVEL               BIT(7)
#define MPU6050_INT_OPEN                BIT(6)
#define MPU6050_LATCH_INT_EN            BIT(5)
#define MPU6050_INT_RD_CLEAR            BIT(4)

/* INT_ENABLE bits */
#define MPU6050_DATA_RDY_EN             BIT(0)

/* INT_STATUS register */
#define MPU6050_INT_STATUS_REG          0x3A
#define MPU6050_INT_DATA_RDY            BIT(0)

/*
 * Sample Rate = Gyroscope Output Rate / (1 + SMPLRT_DIV)
 * Gyro Output Rate = 1000Hz (khi DLPF enabled)
 * 1000 / (1 + 99) = 10Hz
 * 99 = 0x63
 */
#define MPU6050_SMPRT_DIV_10HZ          0x63

/* DLPF Config */
#define MPU6050_DLPF_CFG_42HZ           0x03

/* Sensitivity divisors — tránh magic numbers trong parse */
#define MPU6050_ACCEL_SENSITIVITY_2G    16384
#define MPU6050_GYRO_SENSITIVITY_500    655
#define MPU6050_TEMP_SENSITIVITY        340
#define MPU6050_TEMP_OFFSET             3653

#endif