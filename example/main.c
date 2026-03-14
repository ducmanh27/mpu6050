#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#include "../mpu6050_uapi.h"

#define DEVICE_PATH "/dev/mpu6050-0"

/* ===================== Helper functions ===================== */

static void print_data(const char *label, struct mpu6050_data *data)
{
    printf("\n=== %s ===\n", label);
    printf("Accel X : %6d mg\n",           data->accel_x);
    printf("Accel Y : %6d mg\n",           data->accel_y);
    printf("Accel Z : %6d mg\n",           data->accel_z);
    printf("Temp    : %6d centi-celsius\n", data->temp_centicelsius);
    printf("Gyro  X : %6d mdps\n",         data->gyro_x);
    printf("Gyro  Y : %6d mdps\n",         data->gyro_y);
    printf("Gyro  Z : %6d mdps\n",         data->gyro_z);
}

static int do_read(int fd, struct mpu6050_data *data)
{
    if (read(fd, data, sizeof(*data)) < 0) {
        perror("read failed");
        return -1;
    }
    return 0;
}

static int mpu6050_reset(int fd)
{
    int ret = ioctl(fd, MPU6050_IOC_RESET, 0);
    if (ret < 0)
        perror("ioctl RESET failed");
    return ret;
}

static int mpu6050_sleep(int fd)
{
    int ret = ioctl(fd, MPU6050_IOC_SLEEP, 0);
    if (ret < 0)
        perror("ioctl SLEEP failed");
    return ret;
}

static int mpu6050_wakeup(int fd)
{
    int ret = ioctl(fd, MPU6050_IOC_WAKE_UP, 0);
    if (ret < 0)
        perror("ioctl WAKE_UP failed");
    return ret;
}

static int mpu6050_set_gyro_range(int fd, uint8_t range)
{
    int ret = ioctl(fd, MPU6050_IOC_SET_GYRO_RANGE, &range);
    if (ret < 0)
        perror("ioctl SET_GYRO_RANGE failed");
    return ret;
}

static int mpu6050_get_gyro_range(int fd, uint8_t *range)
{
    int ret = ioctl(fd, MPU6050_IOC_GET_GYRO_RANGE, range);
    if (ret < 0)
        perror("ioctl GET_GYRO_RANGE failed");
    return ret;
}

static int mpu6050_set_accel_range(int fd, uint8_t range)
{
    int ret = ioctl(fd, MPU6050_IOC_SET_ACCEL_RANGE, &range);
    if (ret < 0)
        perror("ioctl SET_ACCEL_RANGE failed");
    return ret;
}

static int mpu6050_get_accel_range(int fd, uint8_t *range)
{
    int ret = ioctl(fd, MPU6050_IOC_GET_ACCEL_RANGE, range);
    if (ret < 0)
        perror("ioctl GET_ACCEL_RANGE failed");
    return ret;
}

/* ===================== Test cases ===================== */

static int test_read_baseline(int fd)
{
    struct mpu6050_data data;
    printf("\n[TEST] Baseline read\n");
    if (do_read(fd, &data) < 0)
        return -1;
    print_data("Baseline", &data);
    return 0;
}

static int test_gyro_range(int fd)
{
    uint8_t set_val, get_val;
    struct mpu6050_data data;

    printf("\n[TEST] Gyro range\n");

    set_val = GYRO_RANGE_1000;
    if (mpu6050_set_gyro_range(fd, set_val) < 0)
        return -1;

    if (mpu6050_get_gyro_range(fd, &get_val) < 0)
        return -1;

    printf("SET gyro range = %d, GET gyro range = %d -> %s\n",
           set_val, get_val,
           set_val == get_val ? "PASS" : "FAIL");

    if (do_read(fd, &data) < 0)
        return -1;
    print_data("After gyro range 1000 dps", &data);
    return 0;
}

static int test_accel_range(int fd)
{
    uint8_t set_val, get_val;
    struct mpu6050_data data;

    printf("\n[TEST] Accel range\n");

    set_val = ACCEL_CONFIG_AFS_SEL_4;
    if (mpu6050_set_accel_range(fd, set_val) < 0)
        return -1;

    if (mpu6050_get_accel_range(fd, &get_val) < 0)
        return -1;

    printf("SET accel range = %d, GET accel range = %d -> %s\n",
           set_val, get_val,
           set_val == get_val ? "PASS" : "FAIL");

    if (do_read(fd, &data) < 0)
        return -1;
    print_data("After accel range 4g", &data);
    return 0;
}

static int test_reset(int fd)
{
    struct mpu6050_data data;

    printf("\n[TEST] Reset\n");
    if (mpu6050_reset(fd) < 0)
        return -1;

    sleep(1);

    if (do_read(fd, &data) < 0)
        return -1;
    print_data("After reset", &data);
    return 0;
}

static int test_sleep_wakeup(int fd)
{
    struct mpu6050_data data;

    printf("\n[TEST] Sleep / Wake up\n");

    if (mpu6050_sleep(fd) < 0)
        return -1;
    printf("Chip is sleeping...\n");
    sleep(1);

    if (mpu6050_wakeup(fd) < 0)
        return -1;
    printf("Chip woke up\n");
    sleep(1);

    if (do_read(fd, &data) < 0)
        return -1;
    print_data("After wake up", &data);
    return 0;
}

/* ===================== Main ===================== */

int main(void)
{
    int fd;
    int failed = 0;

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    printf("Opened %s successfully\n", DEVICE_PATH);

    if (test_read_baseline(fd)  < 0) { failed++; }
    if (test_gyro_range(fd)     < 0) { failed++; }
    if (test_accel_range(fd)    < 0) { failed++; }
    if (test_reset(fd)          < 0) { failed++; }
    if (test_sleep_wakeup(fd)   < 0) { failed++; }

    printf("\n=== Result: %d test(s) failed ===\n", failed);

    close(fd);
    return failed;
}