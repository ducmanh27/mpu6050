#include<linux/module.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/kdev_t.h>
#include<linux/uaccess.h>
#include <linux/platform_device.h>
#include<linux/slab.h>
#include<linux/mod_devicetable.h>
#include<linux/of.h>
#include<linux/of_device.h>
#include<linux/i2c.h>
#include<linux/mutex.h>
#undef pr_fmt
#define pr_fmt(fmt) "%s : " fmt,__func__

#define MAX_DEVICES 10

struct mpu6050dev_private_data {
    struct i2c_client *client;
	dev_t dev_num;   
	struct cdev cdev;
    char *buffer;
    struct mutex lock;
    struct device *device;
};

struct mpu6050drv_private_data {
    int total_devices;
    dev_t device_num_base;
    struct class *class;

};

static struct mpu6050drv_private_data mpu6050_drv_data;

struct of_device_id mpu6050dev_dt_match[];
struct i2c_device_id mpu6050_id_table[];


int mpu6050_i2c_driver_probe(struct i2c_client *client, const struct i2c_device_id *id) {
    pr_info("mpu6050 custom driver probe \n");
    return 0;
}

int mpu6050_i2c_driver_remove(struct i2c_client *client) {
    pr_info("mpu6050 custom driver removed \n");
    return 0;
}

struct of_device_id mpu6050dev_dt_match[] = {
    {.compatible = "invensense,mpu6050-custom", .data = "test"},
    {}
};

struct i2c_device_id mpu6050_id_table[] = {
    {.name = "mpu6050-custom",.driver_data = 123456},
    {}
};

static struct i2c_driver mpu6050_driver = 
{
	.probe = mpu6050_i2c_driver_probe,
	.remove = mpu6050_i2c_driver_remove,
    .id_table = mpu6050_id_table,
	.driver = {
		.name = "mpu6050-char-device",
		.of_match_table = of_match_ptr(mpu6050dev_dt_match)
	}
};



static int __init mpu6050_driver_init(void) {
    int ret;
    // Cấp dải device number cho toàn driver
    ret = alloc_chrdev_region(&mpu6050_drv_data.device_num_base,0,MAX_DEVICES,"mpu6050");
	if(ret < 0){
		pr_err("Alloc chrdev failed\n");
		return ret;
	}

    // Tạo class 1 lần dùng chung
    mpu6050_drv_data.class = class_create(THIS_MODULE, "mpu6050_class");
	if(IS_ERR(mpu6050_drv_data.class)){
		pr_err("Class creation failed\n");
		ret = PTR_ERR(mpu6050_drv_data.class);
		unregister_chrdev_region(mpu6050_drv_data.device_num_base, MAX_DEVICES);
		return ret;
	}
     
    // Đăng ký driver với I2C subsystem
    ret = i2c_add_driver(&mpu6050_driver);
    if (ret < 0) {
        class_destroy(mpu6050_drv_data.class);
        unregister_chrdev_region(mpu6050_drv_data.device_num_base, MAX_DEVICES);
        return ret;
    }

    return ret;
}

static void __exit mpu6050_driver_exit(void) {
    i2c_del_driver(&mpu6050_driver);
	class_destroy(mpu6050_drv_data.class);
	unregister_chrdev_region(mpu6050_drv_data.device_num_base, MAX_DEVICES);
}

module_init(mpu6050_driver_init);
module_exit(mpu6050_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phan Duc Manh");
MODULE_DESCRIPTION("A MPU6050 character device driver");