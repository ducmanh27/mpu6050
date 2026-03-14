#ifndef MPU6050_UAPI_H
#define MPU6050_UAPI_H

#ifdef __KERNEL__
    #include <linux/ioctl.h>
    #include <linux/types.h>
#else
    #include <sys/ioctl.h>
    #include <stdint.h>
    typedef int32_t __s32;
    typedef uint8_t  __u8;
    typedef uint16_t __u16;
    typedef uint32_t __u32;
    typedef int8_t   __s8;
    typedef int16_t  __s16;
    typedef int32_t  __s32;
#endif

/* struct dùng chung */
struct mpu6050_data {
    __s32 accel_x;
    __s32 accel_y;
    __s32 accel_z;
    __s32 temp_centicelsius;
    __s32 gyro_x;
    __s32 gyro_y;
    __s32 gyro_z;
};

enum mpu6050_gyro_range {
    GYRO_RANGE_250 = 0,
    GYRO_RANGE_500,
    GYRO_RANGE_1000,
    GYRO_RANGE_2000
};

enum mpu6050_accel_range {
    ACCEL_CONFIG_AFS_SEL_2 = 0,  
    ACCEL_CONFIG_AFS_SEL_4,
    ACCEL_CONFIG_AFS_SEL_8, 
    ACCEL_CONFIG_AFS_SEL_16 
};
/* ioctl commands */
#define MPU6050_MAGIC 'm'

#define MPU6050_IOC_RESET            _IO(MPU6050_MAGIC,  0)
#define MPU6050_IOC_SLEEP            _IO(MPU6050_MAGIC,  1)
#define MPU6050_IOC_WAKE_UP          _IO(MPU6050_MAGIC,  2)

#define MPU6050_IOC_SET_ACCEL_RANGE _IOW(MPU6050_MAGIC, 3, __u8)
#define MPU6050_IOC_SET_GYRO_RANGE  _IOW(MPU6050_MAGIC, 4, __u8)

#define MPU6050_IOC_GET_CONFIG      _IOR(MPU6050_MAGIC, 5, __u8)
#define MPU6050_IOC_GET_ACCEL_RANGE  _IOR(MPU6050_MAGIC, 6, __u8)
#define MPU6050_IOC_GET_GYRO_RANGE   _IOR(MPU6050_MAGIC, 7, __u8)

#define MPU6050_IOC_MAXNR  7
#endif