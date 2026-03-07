#ifndef MPU6050_H
#define MPU6050_H

#include <linux/bitops.h>

#define MPU6050_CONFIG_REG              0x1A
#define MPU6050_GYRO_CONFIG_REG         0x1B
#define MPU6050_ACCEL_CONFIG_REG        0x1C
#define MPU6050_WHO_AM_I_REG            0x75
#define MPU6050_WHO_AM_I_VALUE          0x68
#define MPU6050_PWR_MGMT_1_REG          0x6B

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

struct mpu6050_data {
    int32_t accel_x;        // đơn vị mg
    int32_t accel_y;
    int32_t accel_z;
    int32_t temp_centicelsius;
    int32_t gyro_x;         // đơn vị mdps
    int32_t gyro_y;
    int32_t gyro_z;
};

#endif