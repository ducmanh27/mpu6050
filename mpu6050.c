#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/poll.h>

#include "mpu6050_uapi.h"
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
    wait_queue_head_t read_queue;
    struct device *device;
    int irq_num;
    bool data_ready;
    spinlock_t s_lock;
    struct mpu6050_data cooked_data;
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
		
int mpu6050_open(struct inode *inode, struct file *filp)
{
	int minor_n;
	
	struct mpu6050dev_private_data *mpu6050dev_data;

    minor_n = MINOR(inode->i_rdev);

    pr_info("minor access = %d\n",minor_n);

    mpu6050dev_data = container_of(inode->i_cdev, struct mpu6050dev_private_data, cdev);
    filp->private_data = mpu6050dev_data;

	return 0;
}

int mpu6050_release(struct inode *inode, struct file *flip)
{
	pr_info("release was successful\n");
	return 0;
}

ssize_t mpu6050_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
    struct mpu6050dev_private_data* mpu6050dev_data = (struct mpu6050dev_private_data*)filp->private_data;

    if (count < sizeof(struct mpu6050_data)) {
        return -EINVAL;
    }

    if ((filp->f_flags & O_NONBLOCK) && !mpu6050dev_data->data_ready)
        return -EAGAIN;

    if (wait_event_interruptible(mpu6050dev_data->read_queue, mpu6050dev_data->data_ready))
        return -ERESTARTSYS;

    if (mutex_lock_interruptible(&mpu6050dev_data->lock))
            return -EINTR;

    if(copy_to_user(buff, &mpu6050dev_data->cooked_data, sizeof(struct mpu6050_data))){
        mutex_unlock(&mpu6050dev_data->lock);
		return -EFAULT;
	}

    mpu6050dev_data->data_ready = false;
    mutex_unlock(&mpu6050dev_data->lock);

	return sizeof(struct mpu6050_data);
}

ssize_t mpu6050_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	return 0;
}
__poll_t mpu6050_poll(struct file *filp, struct poll_table_struct *wait) {
    struct mpu6050dev_private_data *mpu6050dev_data = (struct mpu6050dev_private_data*)filp->private_data;
    unsigned int mask = 0;

    poll_wait(filp, &mpu6050dev_data->read_queue, wait);
    if (mpu6050dev_data->data_ready)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

/* Must be called with dev_data->lock held */
static int mpu6050_write_reg_bitfield(struct i2c_client *client, u8 reg, u8 mask, u8 val)
{
    int ret;
    u8 old_val, new_val;

    /* 1. Đọc giá trị hiện tại từ thanh ghi */
    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0)
        return ret;

    old_val = (u8)ret;

    /* 2. Tính toán giá trị mới: Xóa bit cũ bằng mask và OR với giá trị mới */
    new_val = (old_val & ~mask) | (val & mask);

    /* 3. Chỉ ghi lại nếu giá trị thực sự thay đổi (Tối ưu bus I2C) */
    if (old_val == new_val)
        return 0;

    return i2c_smbus_write_byte_data(client, reg, new_val);
}

static const u8 gyro_range_values[] = {
    MPU6050_GYRO_CONFIG_FS_SEL_250,  
    MPU6050_GYRO_CONFIG_FS_SEL_500,
    MPU6050_GYRO_CONFIG_FS_SEL_1000, 
    MPU6050_GYRO_CONFIG_FS_SEL_2000 
};

static const u8 accel_range_values[] = {
    MPU6050_ACCEL_CONFIG_AFS_SEL_2,  
    MPU6050_ACCEL_CONFIG_AFS_SEL_4,
    MPU6050_ACCEL_CONFIG_AFS_SEL_8, 
    MPU6050_ACCEL_CONFIG_AFS_SEL_16 
};

long mpu6050_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

    int ret = 0;
    struct mpu6050dev_private_data* mpu6050dev_data;
    u8 val;

    if (_IOC_TYPE(cmd) != MPU6050_MAGIC)
        return -ENOTTY;

    if (_IOC_NR(cmd) > MPU6050_IOC_MAXNR)
        return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_WRITE) {
        if (get_user(val, (u8 __user *)arg))
            return -EFAULT;
    }   

    mpu6050dev_data = (struct mpu6050dev_private_data*)filp->private_data;

    if (mutex_lock_interruptible(
        &mpu6050dev_data->lock))
        return -ERESTARTSYS;
    switch (cmd)
    {
    case MPU6050_IOC_RESET:
        ret = i2c_smbus_write_byte_data(mpu6050dev_data->client, 
            MPU6050_PWR_MGMT_1_REG, 
            MPU6050_PWR_MGMT_1_DEVICE_RESET
        );
        if (ret < 0) goto unlock_out;
        msleep(100); 
        ret = i2c_smbus_write_byte_data(mpu6050dev_data->client, 
            MPU6050_PWR_MGMT_1_REG, 
            MPU6050_PWR_MGMT_1_CLKSEL_PLL_X
        );
        break;

    case MPU6050_IOC_SLEEP:
        ret = mpu6050_write_reg_bitfield( mpu6050dev_data->client, 
            MPU6050_PWR_MGMT_1_REG, 
            MPU6050_SLEEP_CONFIG_MASK, 
            MPU6050_PWR_MGMT_1_SLEEP
        );
        break;

    case MPU6050_IOC_WAKE_UP:
        ret = mpu6050_write_reg_bitfield(mpu6050dev_data->client, 
            MPU6050_PWR_MGMT_1_REG, 
            MPU6050_SLEEP_CONFIG_MASK, 0
        );
        if (ret < 0) goto unlock_out;
        msleep(100);
        ret = mpu6050_write_reg_bitfield(mpu6050dev_data->client,
            MPU6050_PWR_MGMT_1_REG,
            MPU6050_CLKSEL_MASK,
            MPU6050_PWR_MGMT_1_CLKSEL_PLL_X);
        break;

    case MPU6050_IOC_SET_ACCEL_RANGE:
        if (val > 3) {
            ret = -EINVAL;
            goto unlock_out;
        }
            ret = mpu6050_write_reg_bitfield(mpu6050dev_data->client, 
                                            MPU6050_ACCEL_CONFIG_REG, 
                                            MPU6050_ACCEL_CONFIG_AFS_SEL_MASK, 
                                            accel_range_values[val]);
        break;

    case MPU6050_IOC_SET_GYRO_RANGE:
        if (val > 3) {
            ret = -EINVAL;
            goto unlock_out;
        }
            ret = mpu6050_write_reg_bitfield(mpu6050dev_data->client, 
                                            MPU6050_GYRO_CONFIG_REG, 
                                            MPU6050_GYRO_CONFIG_FS_SEL_MASK, 
                                            gyro_range_values[val]);
        break;

    case MPU6050_IOC_GET_CONFIG:
        ret = i2c_smbus_read_byte_data(mpu6050dev_data->client, MPU6050_CONFIG_REG);
        if (ret < 0) goto unlock_out;
        if (put_user(ret, (u8 __user *)arg)) {
            ret = -EFAULT;
            goto unlock_out;
        }
        break;

    case MPU6050_IOC_GET_ACCEL_RANGE:
        ret = i2c_smbus_read_byte_data(mpu6050dev_data->client, MPU6050_ACCEL_CONFIG_REG);
        if (ret < 0) goto unlock_out;
        val = (u8)((ret & MPU6050_ACCEL_CONFIG_AFS_SEL_MASK) >> 3);
        if (put_user(val, (u8 __user *)arg)) {
            ret = -EFAULT;
            goto unlock_out;
        }
        ret = 0;
        break;

    case MPU6050_IOC_GET_GYRO_RANGE:
        ret = i2c_smbus_read_byte_data(mpu6050dev_data->client, MPU6050_GYRO_CONFIG_REG);
        if (ret < 0) goto unlock_out;
        val = (u8) ((ret & MPU6050_GYRO_CONFIG_FS_SEL_MASK) >> 3);
        if (put_user(val, (u8 __user *)arg)) {
            ret = -EFAULT;
            goto unlock_out;
        }
        ret = 0;
        break;

    default:
        ret = -ENOTTY;
        break;
    }

unlock_out:
    mutex_unlock(&mpu6050dev_data->lock);
    return (long)ret;
}

static irqreturn_t mpu6050_primary_handler(int irq, void *dev_id) {
    return IRQ_WAKE_THREAD;
}

static irqreturn_t mpu6050_threaded_handler(int irq, void *dev_id)
{
    struct mpu6050dev_private_data *mpu6050dev_data = dev_id;
    int ret;
    u8 raw_buffer[MPU6050_DATA_LEN];
    int16_t raw_accel_x;
    int16_t raw_accel_y;
    int16_t raw_accel_z;
    int16_t temp_raw;
    int16_t raw_gyro_x;
    int16_t raw_gyro_y;
    int16_t raw_gyro_z;

    mutex_lock(&mpu6050dev_data->lock);
    
    ret = i2c_smbus_read_i2c_block_data(mpu6050dev_data->client, MPU6050_DATA_START_REG, 
                                     MPU6050_DATA_LEN, raw_buffer);

    if (ret < 0) {
        dev_err(&mpu6050dev_data->client->dev, "I2C block read failed: %d\n", ret);
        mutex_unlock(&mpu6050dev_data->lock);
        return IRQ_HANDLED;
    }

    raw_accel_x = (int16_t)((raw_buffer[0] << 8) | raw_buffer[1]);
    mpu6050dev_data->cooked_data.accel_x = ((int32_t)raw_accel_x * 1000) / MPU6050_ACCEL_SENSITIVITY_2G;

    raw_accel_y = (int16_t)((raw_buffer[2] << 8) | raw_buffer[3]);
    mpu6050dev_data->cooked_data.accel_y = ((int32_t)raw_accel_y * 1000) / MPU6050_ACCEL_SENSITIVITY_2G;

    raw_accel_z = (int16_t)((raw_buffer[4] << 8) | raw_buffer[5]);
    mpu6050dev_data->cooked_data.accel_z = ((int32_t)raw_accel_z * 1000) / MPU6050_ACCEL_SENSITIVITY_2G;

    // Nhiệt độ (Register 0x41 và 0x42)
    temp_raw = (int16_t)((raw_buffer[6] << 8) | raw_buffer[7]);
    mpu6050dev_data->cooked_data.temp_centicelsius = ((int32_t)temp_raw * 100) / MPU6050_TEMP_SENSITIVITY  + MPU6050_TEMP_OFFSET;

    // Gyroscope (Register 0x43 đến 0x48)
    // gyro_mdps = gyro_raw * 1000 * 10 / 655
    raw_gyro_x = (int16_t)((raw_buffer[8] << 8) | raw_buffer[9]);
    mpu6050dev_data->cooked_data.gyro_x  = ((int32_t)raw_gyro_x * 10000) / MPU6050_GYRO_SENSITIVITY_500;

    raw_gyro_y = (int16_t)((raw_buffer[10] << 8) | raw_buffer[11]);
    mpu6050dev_data->cooked_data.gyro_y  = ((int32_t)raw_gyro_y * 10000) / MPU6050_GYRO_SENSITIVITY_500;

    raw_gyro_z = (int16_t)((raw_buffer[12] << 8) | raw_buffer[13]);
    mpu6050dev_data->cooked_data.gyro_z  = ((int32_t)raw_gyro_z * 10000) / MPU6050_GYRO_SENSITIVITY_500;

    mutex_unlock(&mpu6050dev_data->lock);
    mpu6050dev_data->data_ready = true;
    wake_up_interruptible(&mpu6050dev_data->read_queue);
    return IRQ_HANDLED;
}


struct file_operations mpu6050_ops = {
	.open = mpu6050_open,
	.release = mpu6050_release,
	.read = mpu6050_read,
	.write = mpu6050_write,
	.llseek = mpu6050_lseek,
    .unlocked_ioctl = mpu6050_ioctl,
    .poll = mpu6050_poll,
    .owner = THIS_MODULE,
};


int mpu6050_i2c_driver_probe(struct i2c_client *client, const struct i2c_device_id *id) {

    int ret;

    struct mpu6050dev_private_data *dev_data;

    struct device *dev = &client->dev;

    void* driver_data;

    const struct of_device_id *match;
    
    int irq_num;

    pr_info("A device is detected\n");

    match = of_match_device(of_match_ptr(mpu6050dev_dt_match), dev);

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

    irq_num = client->irq; 
    
    if (irq_num < 0) {
        dev_err(&client->dev, "Failed to get IRQ number\n");
        return irq_num;
    }
    

    dev_data->irq_num = irq_num;

    spin_lock_init(&dev_data->s_lock);
    mutex_init(&dev_data->lock);
    init_waitqueue_head(&dev_data->read_queue);
    dev_data->data_ready = false;

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
    // 1. Cấu hình Gyro: Chọn ± 500 °/s (FS_SEL = 1) 
    // Giá trị: 000 01 000 = 0x08
    ret = i2c_smbus_write_byte_data(client, MPU6050_GYRO_CONFIG_REG, MPU6050_GYRO_CONFIG_FS_SEL_500);
    if (ret < 0) {
    dev_err(dev, "Gyro config failed\n");
        return ret;
    }
    // 2. Cấu hình Accel: Chọn ± 2g (AFS_SEL = 0) */
    // Giá trị: 000 00 000 = 0x00
    ret  = i2c_smbus_write_byte_data(client, MPU6050_ACCEL_CONFIG_REG, MPU6050_ACCEL_CONFIG_AFS_SEL_2);
    if (ret < 0) {
    dev_err(dev, "Accel config failed\n");
        return ret;
    }

    ret = i2c_smbus_write_byte_data(client,
        MPU6050_INT_PIN_CFG_REG,
        MPU6050_INT_LEVEL | MPU6050_INT_RD_CLEAR);
    if (ret < 0) {
    dev_err(dev, "Interrupt pin config failed\n");
        return ret;
    }

    ret = i2c_smbus_write_byte_data(client,
        MPU6050_INT_ENABLE_REG,
        MPU6050_DATA_RDY_EN);
    if (ret < 0) {
    dev_err(dev, "Interrupt enbale failed\n");
        return ret;
    } 

    // Sample rate 10Hz 
    i2c_smbus_write_byte_data(client,
        MPU6050_SMPRT_DIV_REG,
        MPU6050_SMPRT_DIV_10HZ);

    // DLPF 42Hz 
    i2c_smbus_write_byte_data(client,
        MPU6050_CONFIG_REG,
        MPU6050_DLPF_CFG_42HZ);
    
    i2c_set_clientdata(client, dev_data);

    dev_data->client = client;

    ret = request_threaded_irq(client->irq, 
                            mpu6050_primary_handler,   /* Top Half */
                            mpu6050_threaded_handler,  /* Bottom Half */
                            IRQF_TRIGGER_FALLING | IRQF_ONESHOT, 
                            "mpu6050_event", 
                            dev_data);
    if (ret) {
        dev_err(&client->dev, "Failed to register threaded irq\n");
        return ret;
    }

    dev_data->dev_num = mpu6050_drv_data.device_num_base + mpu6050_drv_data.total_devices;

    cdev_init(&dev_data->cdev, &mpu6050_ops);

    dev_data->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev_data->cdev, dev_data->dev_num, 1);
	if(ret < 0){
		dev_err(dev,"Cdev add failed\n");
        free_irq(dev_data->irq_num, dev_data);
		return ret;
	}

    dev_data->device = device_create(mpu6050_drv_data.class, dev, dev_data->dev_num,NULL,\
								"mpu6050-%d", mpu6050_drv_data.total_devices);
	if(IS_ERR(dev_data->device)){
		dev_err(dev,"Device create failed\n");
        free_irq(dev_data->irq_num, dev_data);
		cdev_del(&dev_data->cdev);
        ret = PTR_ERR(dev_data->device);
		return ret;
	}

    mpu6050_drv_data.total_devices++;

    dev_info(dev,"Probe was successful\n");

    return 0;
}


int mpu6050_i2c_driver_remove(struct i2c_client *client) {
	struct mpu6050dev_private_data  *dev_data = i2c_get_clientdata(client);

    int ret;
    i2c_smbus_write_byte_data(client,
        MPU6050_INT_ENABLE_REG, 0);

    ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG, MPU6050_PWR_MGMT_1_SLEEP);
    if (ret < 0) {
        dev_warn(&client->dev, "Failed to sleep chip, hardware may be unavailable\n");
    }
    free_irq(dev_data->irq_num, dev_data);

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