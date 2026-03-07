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
#include <linux/delay.h>

#include "mpu6050.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s : " fmt,__func__

#define MAX_DEVICES 10

struct mpu6050dev_private_data {
    struct i2c_client *client;
	dev_t dev_num;   
	struct cdev cdev;
    char buffer[14];
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

loff_t mpu6050_lseek(struct file *filp, loff_t offset, int whence)
{
	return 0;
}

ssize_t mpu6050_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
	return 0;

}

ssize_t mpu6050_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	return 0;
}


		
int mpu6050_open(struct inode *inode, struct file *filp)
{
	return 0;
}

int mpu6050_release(struct inode *inode, struct file *flip)
{
	pr_info("release was successful\n");

	return 0;
}

struct file_operations mpu6050_ops = {
	.open = mpu6050_open,
	.release = mpu6050_release,
	.read = mpu6050_read,
	.write = mpu6050_write,
	.llseek = mpu6050_lseek,
	.owner = THIS_MODULE
};


int mpu6050_i2c_driver_probe(struct i2c_client *client, const struct i2c_device_id *id) {

    int ret;

    struct mpu6050dev_private_data *dev_data;

    struct device *dev = &client->dev;

    void* driver_data;

    const struct of_device_id *match;

    pr_info("A device is detected\n");

    match = of_match_device(of_match_ptr(mpu6050dev_dt_match),dev);

    if(match) {
		driver_data = match->data;
	} else {
        dev_info(dev, "No driver match\n");
		return -EINVAL;
	}

	dev_data = devm_kzalloc(&client->dev, sizeof(*dev_data),GFP_KERNEL);
	if(!dev_data) {
		dev_info(dev,"Cannot allocate memory \n");
		return -ENOMEM;
	}

    ret = i2c_smbus_read_byte_data(client, MPU6050_WHO_AM_I_REG);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read WHO_AM_I register\n");
        return ret;
    }
    
    if (ret != MPU6050_WHO_AM_I_VALUE) { 
        dev_err(&client->dev, "Device ID mismatch (0x%02x)\n", ret);
        return -ENODEV;
    }

    ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG, MPU6050_PWR_MGMT_1_DEVICE_RESET); 
    if (ret < 0) {
    dev_err(&client->dev, "Reset failed\n");
        return ret;
    }

    msleep(100); // Đợi chip reset xong
    // 0x01: SLEEP=0, CYCLE=0, TEMP_DIS=0, CLKSEL=1
    ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG, MPU6050_PWR_MGMT_1_CLKSEL_PLL_X);
    if (ret < 0) {
        dev_err(&client->dev, "Configure failed\n");
        return ret;
    }
    msleep(10);

    i2c_set_clientdata(client, dev_data);

    dev_data->client = client;

    dev_data->dev_num = mpu6050_drv_data.device_num_base + mpu6050_drv_data.total_devices;

    cdev_init(&dev_data->cdev, &mpu6050_ops);

    dev_data->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev_data->cdev, dev_data->dev_num, 1);
	if(ret < 0){
		dev_err(dev,"Cdev add failed\n");
		return ret;
	}

    dev_data->device = device_create(mpu6050_drv_data.class, dev, dev_data->dev_num,NULL,\
								"mpu6050-%d", mpu6050_drv_data.total_devices);
	if(IS_ERR(dev_data->device)){
		dev_err(dev,"Device create failed\n");
		ret = PTR_ERR(dev_data->device);
		cdev_del(&dev_data->cdev);
		return ret;
	}

    mpu6050_drv_data.total_devices++;

    dev_info(dev,"Probe was successful\n");

    return 0;
}


int mpu6050_i2c_driver_remove(struct i2c_client *client) {
	struct mpu6050dev_private_data  *dev_data = i2c_get_clientdata(client);

    int ret;

    ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG, MPU6050_PWR_MGMT_1_SLEEP);
    if (ret < 0) {
        dev_err(&client->dev, "Configure failed\n");
        return ret;
    }

	/*1. Remove a device that was created with device_create() */
	device_destroy(mpu6050_drv_data.class, dev_data->dev_num);
	
	/*2. Remove a cdev entry from the system*/
	cdev_del(&dev_data->cdev);

	mpu6050_drv_data.total_devices--;

    
	dev_info(&client->dev,"A device is removed\n");
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