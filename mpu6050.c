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
#define pr_fmt(fmt) "%s : " fmt, __func__

#define MAX_DEVICES 10

/* =====================================================================
 * Data Structures
 * ===================================================================== */

struct mpu6050dev_private_data {
	struct i2c_client    *client;
	dev_t                 dev_num;
	struct cdev           cdev;
	char                  buffer[14];
	struct mutex          lock;
	wait_queue_head_t     read_queue;
	struct device        *device;
	int                   irq_num;
	bool                  data_ready;
	struct mpu6050_data   cooked_data;
	u8                    chip_id;
};

struct mpu6050drv_private_data {
	int            total_devices;
	dev_t          device_num_base;
	struct class  *class;
};

static struct mpu6050drv_private_data mpu6050_drv_data;

struct of_device_id mpu6050dev_dt_match[];
struct i2c_device_id mpu6050_id_table[];

/* =====================================================================
 * Lookup Tables — dùng chung cho cả sysfs và ioctl
 * ===================================================================== */

struct mpu6050_range_map {
	int user_val;   /* giá trị người dùng nhìn thấy (dps hoặc g) */
	u8  reg_val;    /* giá trị ghi vào register */
	u8  index;      /* index 0-3 dùng trong ioctl */
};

static const struct mpu6050_range_map gyro_range_table[] = {
	{ 250,  MPU6050_GYRO_CONFIG_FS_SEL_250,  0 },
	{ 500,  MPU6050_GYRO_CONFIG_FS_SEL_500,  1 },
	{ 1000, MPU6050_GYRO_CONFIG_FS_SEL_1000, 2 },
	{ 2000, MPU6050_GYRO_CONFIG_FS_SEL_2000, 3 },
};

static const struct mpu6050_range_map accel_range_table[] = {
	{ 2,  MPU6050_ACCEL_CONFIG_AFS_SEL_2,  0 },
	{ 4,  MPU6050_ACCEL_CONFIG_AFS_SEL_4,  1 },
	{ 8,  MPU6050_ACCEL_CONFIG_AFS_SEL_8,  2 },
	{ 16, MPU6050_ACCEL_CONFIG_AFS_SEL_16, 3 },
};

/* =====================================================================
 * Helper Functions
 * ===================================================================== */

/**
 * mpu6050_write_reg_bitfield - Read-modify-write một register
 * Phải được gọi khi đang giữ dev_data->lock
 */
static int mpu6050_write_reg_bitfield(struct i2c_client *client,
				      u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 old_val, new_val;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		return ret;

	old_val = (u8)ret;
	new_val = (old_val & ~mask) | (val & mask);

	/* Chỉ ghi nếu thực sự thay đổi — tối ưu bus I2C */
	if (old_val == new_val)
		return 0;

	return i2c_smbus_write_byte_data(client, reg, new_val);
}

/**
 * gyro_index_to_reg - Lấy reg_val từ ioctl index (0-3)
 */
static int gyro_index_to_reg(u8 index, u8 *reg_val)
{
	if (index >= ARRAY_SIZE(gyro_range_table))
		return -EINVAL;
	*reg_val = gyro_range_table[index].reg_val;
	return 0;
}

/**
 * accel_index_to_reg - Lấy reg_val từ ioctl index (0-3)
 */
static int accel_index_to_reg(u8 index, u8 *reg_val)
{
	if (index >= ARRAY_SIZE(accel_range_table))
		return -EINVAL;
	*reg_val = accel_range_table[index].reg_val;
	return 0;
}

/**
 * get_gyro_out_rate - Tính Gyro Output Rate dựa trên DLPF config
 */
static int get_gyro_out_rate(u8 config)
{
	u8 dlpf = config & 0x07;

	/* DLPF = 0 hoặc 7 → 8kHz, ngược lại → 1kHz */
	return (dlpf == 0 || dlpf == 7) ? 8000 : 1000;
}

/* =====================================================================
 * Sysfs Attributes
 * ===================================================================== */

static ssize_t chip_id_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);

	return sprintf(buf, "0x%02x\n", dev_data->chip_id);
}

static ssize_t temperature_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int ret;
	s16 temp_raw;
	int temp_centi;

	mutex_lock(&dev_data->lock);
	ret = i2c_smbus_read_word_data(dev_data->client, MPU6050_TEMP_OUT_H_REG);
	mutex_unlock(&dev_data->lock);

	if (ret < 0)
		return ret;

	temp_raw      = (s16)be16_to_cpu((u16)ret);
	temp_centi    = ((int32_t)temp_raw * 100) / MPU6050_TEMP_SENSITIVITY
			+ MPU6050_TEMP_OFFSET;

	return sprintf(buf, "%d\n", temp_centi);
}

static ssize_t gyro_range_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int ret, i;
	u8 reg_val;

	mutex_lock(&dev_data->lock);
	ret = i2c_smbus_read_byte_data(dev_data->client, MPU6050_GYRO_CONFIG_REG);
	mutex_unlock(&dev_data->lock);

	if (ret < 0)
		return ret;

	reg_val = (u8)ret & MPU6050_GYRO_CONFIG_FS_SEL_MASK;

	for (i = 0; i < ARRAY_SIZE(gyro_range_table); i++) {
		if (reg_val == gyro_range_table[i].reg_val)
			return sprintf(buf, "%d\n", gyro_range_table[i].user_val);
	}

	return sprintf(buf, "unknown\n");
}

static ssize_t gyro_range_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int user_input, ret, i;
	u8 reg_val = 0xFF;

	ret = kstrtoint(buf, 0, &user_input);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(gyro_range_table); i++) {
		if (user_input == gyro_range_table[i].user_val) {
			reg_val = gyro_range_table[i].reg_val;
			break;
		}
	}

	if (reg_val == 0xFF) {
		dev_err(dev, "Invalid gyro range: %d. Supported: 250, 500, 1000, 2000\n",
			user_input);
		return -EINVAL;
	}

	mutex_lock(&dev_data->lock);
	ret = mpu6050_write_reg_bitfield(dev_data->client,
					 MPU6050_GYRO_CONFIG_REG,
					 MPU6050_GYRO_CONFIG_FS_SEL_MASK,
					 reg_val);
	mutex_unlock(&dev_data->lock);

	if (ret < 0)
		return ret;

	dev_info(dev, "Gyro range updated to %d dps\n", user_input);
	return count;
}

static ssize_t accel_range_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int ret, i;
	u8 reg_val;

	mutex_lock(&dev_data->lock);
	ret = i2c_smbus_read_byte_data(dev_data->client, MPU6050_ACCEL_CONFIG_REG);
	mutex_unlock(&dev_data->lock);

	if (ret < 0)
		return ret;

	reg_val = (u8)ret & MPU6050_ACCEL_CONFIG_AFS_SEL_MASK;

	for (i = 0; i < ARRAY_SIZE(accel_range_table); i++) {
		if (reg_val == accel_range_table[i].reg_val)
			return sprintf(buf, "%d\n", accel_range_table[i].user_val);
	}

	return sprintf(buf, "unknown\n");
}

static ssize_t accel_range_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int user_input, ret, i;
	u8 reg_val = 0xFF;

	ret = kstrtoint(buf, 0, &user_input);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(accel_range_table); i++) {
		if (user_input == accel_range_table[i].user_val) {
			reg_val = accel_range_table[i].reg_val;
			break;
		}
	}

	if (reg_val == 0xFF) {
		dev_err(dev, "Invalid accel range: %d. Supported: 2, 4, 8, 16\n",
			user_input);
		return -EINVAL;
	}

	mutex_lock(&dev_data->lock);
	ret = mpu6050_write_reg_bitfield(dev_data->client,
					 MPU6050_ACCEL_CONFIG_REG,
					 MPU6050_ACCEL_CONFIG_AFS_SEL_MASK,
					 reg_val);
	mutex_unlock(&dev_data->lock);

	if (ret < 0)
		return ret;

	dev_info(dev, "Accel range updated to +/-%dg\n", user_input);
	return count;
}

static ssize_t sample_rate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int smplrt_div, config;

	mutex_lock(&dev_data->lock);
	smplrt_div = i2c_smbus_read_byte_data(dev_data->client, MPU6050_SMPRT_DIV_REG);
	config     = i2c_smbus_read_byte_data(dev_data->client, MPU6050_CONFIG_REG);
	mutex_unlock(&dev_data->lock);

	if (smplrt_div < 0 || config < 0)
		return -EIO;

	return sprintf(buf, "%d\n",
		       get_gyro_out_rate((u8)config) / (1 + (u8)smplrt_div));
}

static ssize_t sample_rate_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct mpu6050dev_private_data *dev_data = dev_get_drvdata(dev);
	int target_rate, config, ret, div;

	ret = kstrtoint(buf, 0, &target_rate);
	if (ret)
		return ret;

	if (target_rate <= 0)
		return -EINVAL;

	mutex_lock(&dev_data->lock);

	config = i2c_smbus_read_byte_data(dev_data->client, MPU6050_CONFIG_REG);
	if (config < 0) {
		mutex_unlock(&dev_data->lock);
		return config;
	}

	/* DIV = (Gyro_Rate / Target_Rate) - 1 */
	div = (get_gyro_out_rate((u8)config) / target_rate) - 1;

	if (div < 0 || div > 255) {
		dev_err(dev, "Sample rate %d Hz out of valid range\n", target_rate);
		mutex_unlock(&dev_data->lock);
		return -EINVAL;
	}

	ret = i2c_smbus_write_byte_data(dev_data->client,
					MPU6050_SMPRT_DIV_REG, (u8)div);
	mutex_unlock(&dev_data->lock);

	return (ret < 0) ? ret : count;
}

static DEVICE_ATTR_RO(chip_id);
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RW(gyro_range);
static DEVICE_ATTR_RW(accel_range);
static DEVICE_ATTR_RW(sample_rate);

static struct attribute *mpu6050_attrs[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_temperature.attr,
	&dev_attr_gyro_range.attr,
	&dev_attr_accel_range.attr,
	&dev_attr_sample_rate.attr,
	NULL
};

static const struct attribute_group mpu6050_attr_group = {
	.attrs = mpu6050_attrs,
};

static const struct attribute_group *mpu6050_attr_groups[] = {
	&mpu6050_attr_group,
	NULL
};

/* =====================================================================
 * File Operations
 * ===================================================================== */

static loff_t mpu6050_lseek(struct file *filp, loff_t offset, int whence)
{
	return 0;
}

static int mpu6050_open(struct inode *inode, struct file *filp)
{
	struct mpu6050dev_private_data *dev_data;

	pr_info("minor access = %d\n", MINOR(inode->i_rdev));

	dev_data = container_of(inode->i_cdev,
				struct mpu6050dev_private_data, cdev);
	filp->private_data = dev_data;

	return 0;
}

static int mpu6050_release(struct inode *inode, struct file *filp)
{
	pr_info("release was successful\n");
	return 0;
}

static ssize_t mpu6050_read(struct file *filp, char __user *buff,
			    size_t count, loff_t *f_pos)
{
	struct mpu6050dev_private_data *dev_data = filp->private_data;

	if (count < sizeof(struct mpu6050_data))
		return -EINVAL;

	if ((filp->f_flags & O_NONBLOCK) && !dev_data->data_ready)
		return -EAGAIN;

	if (wait_event_interruptible(dev_data->read_queue, dev_data->data_ready))
		return -ERESTARTSYS;

	if (mutex_lock_interruptible(&dev_data->lock))
		return -EINTR;

	if (copy_to_user(buff, &dev_data->cooked_data, sizeof(struct mpu6050_data))) {
		mutex_unlock(&dev_data->lock);
		return -EFAULT;
	}

	dev_data->data_ready = false;
	mutex_unlock(&dev_data->lock);

	return sizeof(struct mpu6050_data);
}

static ssize_t mpu6050_write(struct file *filp, const char __user *buff,
			     size_t count, loff_t *f_pos)
{
	return 0;
}

static __poll_t mpu6050_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct mpu6050dev_private_data *dev_data = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &dev_data->read_queue, wait);

	if (dev_data->data_ready)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static long mpu6050_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct mpu6050dev_private_data *dev_data = filp->private_data;
	int ret = 0;
	u8 val, reg_val;

	if (_IOC_TYPE(cmd) != MPU6050_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > MPU6050_IOC_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (get_user(val, (u8 __user *)arg))
			return -EFAULT;
	}

	if (mutex_lock_interruptible(&dev_data->lock))
		return -ERESTARTSYS;

	switch (cmd) {
	case MPU6050_IOC_RESET:
		ret = i2c_smbus_write_byte_data(dev_data->client,
						MPU6050_PWR_MGMT_1_REG,
						MPU6050_PWR_MGMT_1_DEVICE_RESET);
		if (ret < 0) goto unlock_out;
		msleep(100);
		ret = i2c_smbus_write_byte_data(dev_data->client,
						MPU6050_PWR_MGMT_1_REG,
						MPU6050_PWR_MGMT_1_CLKSEL_PLL_X);
		break;

	case MPU6050_IOC_SLEEP:
		ret = mpu6050_write_reg_bitfield(dev_data->client,
						 MPU6050_PWR_MGMT_1_REG,
						 MPU6050_SLEEP_CONFIG_MASK,
						 MPU6050_PWR_MGMT_1_SLEEP);
		break;

	case MPU6050_IOC_WAKE_UP:
		ret = mpu6050_write_reg_bitfield(dev_data->client,
						 MPU6050_PWR_MGMT_1_REG,
						 MPU6050_SLEEP_CONFIG_MASK, 0);
		if (ret < 0) goto unlock_out;
		msleep(100);
		ret = mpu6050_write_reg_bitfield(dev_data->client,
						 MPU6050_PWR_MGMT_1_REG,
						 MPU6050_CLKSEL_MASK,
						 MPU6050_PWR_MGMT_1_CLKSEL_PLL_X);
		break;

	case MPU6050_IOC_SET_ACCEL_RANGE:
		ret = accel_index_to_reg(val, &reg_val);
		if (ret < 0) goto unlock_out;
		ret = mpu6050_write_reg_bitfield(dev_data->client,
						 MPU6050_ACCEL_CONFIG_REG,
						 MPU6050_ACCEL_CONFIG_AFS_SEL_MASK,
						 reg_val);
		break;

	case MPU6050_IOC_SET_GYRO_RANGE:
		ret = gyro_index_to_reg(val, &reg_val);
		if (ret < 0) goto unlock_out;
		ret = mpu6050_write_reg_bitfield(dev_data->client,
						 MPU6050_GYRO_CONFIG_REG,
						 MPU6050_GYRO_CONFIG_FS_SEL_MASK,
						 reg_val);
		break;

	case MPU6050_IOC_GET_CONFIG:
		ret = i2c_smbus_read_byte_data(dev_data->client, MPU6050_CONFIG_REG);
		if (ret < 0) goto unlock_out;
		if (put_user((u8)ret, (u8 __user *)arg)) {
			ret = -EFAULT;
			goto unlock_out;
		}
		ret = 0;
		break;

	case MPU6050_IOC_GET_ACCEL_RANGE:
		ret = i2c_smbus_read_byte_data(dev_data->client, MPU6050_ACCEL_CONFIG_REG);
		if (ret < 0) goto unlock_out;
		val = (u8)((ret & MPU6050_ACCEL_CONFIG_AFS_SEL_MASK) >> 3);
		if (put_user(val, (u8 __user *)arg)) {
			ret = -EFAULT;
			goto unlock_out;
		}
		ret = 0;
		break;

	case MPU6050_IOC_GET_GYRO_RANGE:
		ret = i2c_smbus_read_byte_data(dev_data->client, MPU6050_GYRO_CONFIG_REG);
		if (ret < 0) goto unlock_out;
		val = (u8)((ret & MPU6050_GYRO_CONFIG_FS_SEL_MASK) >> 3);
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
	mutex_unlock(&dev_data->lock);
	return (long)ret;
}

static struct file_operations mpu6050_ops = {
	.owner          = THIS_MODULE,
	.open           = mpu6050_open,
	.release        = mpu6050_release,
	.read           = mpu6050_read,
	.write          = mpu6050_write,
	.llseek         = mpu6050_lseek,
	.unlocked_ioctl = mpu6050_ioctl,
	.poll           = mpu6050_poll,
};

/* =====================================================================
 * IRQ Handlers
 * ===================================================================== */

static irqreturn_t mpu6050_primary_handler(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t mpu6050_threaded_handler(int irq, void *dev_id)
{
	struct mpu6050dev_private_data *dev_data = dev_id;
	u8 raw_buffer[MPU6050_DATA_LEN];
	int16_t raw;
	int ret;

	mutex_lock(&dev_data->lock);

	ret = i2c_smbus_read_i2c_block_data(dev_data->client,
					    MPU6050_DATA_START_REG,
					    MPU6050_DATA_LEN,
					    raw_buffer);
	if (ret < 0) {
		dev_err(&dev_data->client->dev,
			"I2C block read failed: %d\n", ret);
		mutex_unlock(&dev_data->lock);
		return IRQ_HANDLED;
	}

	/* Accel X, Y, Z */
	raw = (int16_t)((raw_buffer[0] << 8) | raw_buffer[1]);
	dev_data->cooked_data.accel_x =
		((int32_t)raw * 1000) / MPU6050_ACCEL_SENSITIVITY_2G;

	raw = (int16_t)((raw_buffer[2] << 8) | raw_buffer[3]);
	dev_data->cooked_data.accel_y =
		((int32_t)raw * 1000) / MPU6050_ACCEL_SENSITIVITY_2G;

	raw = (int16_t)((raw_buffer[4] << 8) | raw_buffer[5]);
	dev_data->cooked_data.accel_z =
		((int32_t)raw * 1000) / MPU6050_ACCEL_SENSITIVITY_2G;

	/* Temperature */
	raw = (int16_t)((raw_buffer[6] << 8) | raw_buffer[7]);
	dev_data->cooked_data.temp_centicelsius =
		((int32_t)raw * 100) / MPU6050_TEMP_SENSITIVITY + MPU6050_TEMP_OFFSET;

	/* Gyro X, Y, Z */
	raw = (int16_t)((raw_buffer[8] << 8) | raw_buffer[9]);
	dev_data->cooked_data.gyro_x =
		((int32_t)raw * 10000) / MPU6050_GYRO_SENSITIVITY_500;

	raw = (int16_t)((raw_buffer[10] << 8) | raw_buffer[11]);
	dev_data->cooked_data.gyro_y =
		((int32_t)raw * 10000) / MPU6050_GYRO_SENSITIVITY_500;

	raw = (int16_t)((raw_buffer[12] << 8) | raw_buffer[13]);
	dev_data->cooked_data.gyro_z =
		((int32_t)raw * 10000) / MPU6050_GYRO_SENSITIVITY_500;

	mutex_unlock(&dev_data->lock);

	dev_data->data_ready = true;
	wake_up_interruptible(&dev_data->read_queue);

	return IRQ_HANDLED;
}

/* =====================================================================
 * I2C Driver — probe / remove
 * ===================================================================== */

static int mpu6050_hw_init(struct i2c_client *client)
{
	int ret;

	/* Reset chip */
	ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG,
					MPU6050_PWR_MGMT_1_DEVICE_RESET);
	if (ret < 0) return ret;
	msleep(100);

	/* Wake up + PLL clock */
	ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG,
					MPU6050_PWR_MGMT_1_CLKSEL_PLL_X);
	if (ret < 0) return ret;
	msleep(10);

	/* Gyro: ±500 dps */
	ret = i2c_smbus_write_byte_data(client, MPU6050_GYRO_CONFIG_REG,
					MPU6050_GYRO_CONFIG_FS_SEL_500);
	if (ret < 0) return ret;

	/* Accel: ±2g */
	ret = i2c_smbus_write_byte_data(client, MPU6050_ACCEL_CONFIG_REG,
					MPU6050_ACCEL_CONFIG_AFS_SEL_2);
	if (ret < 0) return ret;

	/* INT pin: active LOW, clear on any read */
	ret = i2c_smbus_write_byte_data(client, MPU6050_INT_PIN_CFG_REG,
					MPU6050_INT_LEVEL | MPU6050_INT_RD_CLEAR);
	if (ret < 0) return ret;

	/* Enable DATA_RDY interrupt */
	ret = i2c_smbus_write_byte_data(client, MPU6050_INT_ENABLE_REG,
					MPU6050_DATA_RDY_EN);
	if (ret < 0) return ret;

	/* Sample rate: 10Hz */
	i2c_smbus_write_byte_data(client, MPU6050_SMPRT_DIV_REG,
				  MPU6050_SMPRT_DIV_10HZ);

	/* DLPF: 42Hz bandwidth */
	i2c_smbus_write_byte_data(client, MPU6050_CONFIG_REG,
				  MPU6050_DLPF_CFG_42HZ);

	/* Đợi chip stable trước khi IRQ được đăng ký */
	msleep(50);

	return 0;
}

int mpu6050_i2c_driver_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct mpu6050dev_private_data *dev_data;
	struct device *dev = &client->dev;
	const struct of_device_id *match;
	int ret;

	pr_info("A device is detected\n");

	match = of_match_device(of_match_ptr(mpu6050dev_dt_match), dev);
	if (!match) {
		dev_info(dev, "No DT match\n");
		return -EINVAL;
	}

	/* Alloc per-device struct */
	dev_data = devm_kzalloc(dev, sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data) {
		dev_err(dev, "Cannot allocate memory\n");
		return -ENOMEM;
	}

	/* Lấy IRQ number từ DT — phải check trước khi làm gì khác */
	if (client->irq < 0) {
		dev_err(dev, "Failed to get IRQ number\n");
		return client->irq;
	}

	/* Gán client NGAY — tránh NULL ptr khi threaded handler chạy sớm */
	dev_data->client  = client;
	dev_data->irq_num = client->irq;

	i2c_set_clientdata(client, dev_data);

	/* Init synchronization primitives */
	mutex_init(&dev_data->lock);
	init_waitqueue_head(&dev_data->read_queue);
	dev_data->data_ready = false;

	/* Verify chip */
	ret = i2c_smbus_read_byte_data(client, MPU6050_WHO_AM_I_REG);
	if (ret < 0) {
		dev_err(dev, "Failed to read WHO_AM_I\n");
		return ret;
	}
	if (ret != MPU6050_WHO_AM_I_VALUE) {
		dev_err(dev, "Device ID mismatch: 0x%02x\n", ret);
		return -ENODEV;
	}
	dev_data->chip_id = (u8)ret;
	dev_info(dev, "Found MPU6050 chip ID: 0x%02x\n", dev_data->chip_id);

	/* Init hardware */
	ret = mpu6050_hw_init(client);
	if (ret < 0) {
		dev_err(dev, "Hardware init failed: %d\n", ret);
		return ret;
	}

	/* Đăng ký IRQ — sau khi hardware đã config xong */
	ret = request_threaded_irq(client->irq,
				   mpu6050_primary_handler,
				   mpu6050_threaded_handler,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "mpu6050_event",
				   dev_data);
	if (ret) {
		dev_err(dev, "Failed to register IRQ: %d\n", ret);
		return ret;
	}

	/* Char device setup */
	dev_data->dev_num = mpu6050_drv_data.device_num_base
			    + mpu6050_drv_data.total_devices;

	cdev_init(&dev_data->cdev, &mpu6050_ops);
	dev_data->cdev.owner = THIS_MODULE;

	ret = cdev_add(&dev_data->cdev, dev_data->dev_num, 1);
	if (ret < 0) {
		dev_err(dev, "cdev_add failed\n");
		free_irq(dev_data->irq_num, dev_data);
		return ret;
	}

	/* Tạo device node + sysfs attributes trong 1 lần gọi */
	dev_data->device = device_create_with_groups(
				mpu6050_drv_data.class, dev,
				dev_data->dev_num, dev_data,
				mpu6050_attr_groups,
				"mpu6050-%d", mpu6050_drv_data.total_devices);
	if (IS_ERR(dev_data->device)) {
		dev_err(dev, "device_create failed\n");
		free_irq(dev_data->irq_num, dev_data);
		cdev_del(&dev_data->cdev);
		return PTR_ERR(dev_data->device);
	}

	mpu6050_drv_data.total_devices++;
	dev_info(dev, "Probe successful\n");

	return 0;
}

int mpu6050_i2c_driver_remove(struct i2c_client *client)
{
	struct mpu6050dev_private_data *dev_data = i2c_get_clientdata(client);
	int ret;

	/* Disable chip interrupt trước — chip không generate IRQ nữa */
	i2c_smbus_write_byte_data(client, MPU6050_INT_ENABLE_REG, 0);

	/* Sleep chip */
	ret = i2c_smbus_write_byte_data(client, MPU6050_PWR_MGMT_1_REG,
					MPU6050_PWR_MGMT_1_SLEEP);
	if (ret < 0)
		dev_warn(&client->dev, "Failed to sleep chip\n");

	/* Free IRQ sau khi chip đã silent */
	free_irq(dev_data->irq_num, dev_data);

	device_destroy(mpu6050_drv_data.class, dev_data->dev_num);
	cdev_del(&dev_data->cdev);
	mpu6050_drv_data.total_devices--;

	dev_info(&client->dev, "Device removed\n");
	return 0;
}

/* =====================================================================
 * Driver Registration
 * ===================================================================== */

struct of_device_id mpu6050dev_dt_match[] = {
	{ .compatible = "invensense,mpu6050-custom", .data = "test" },
	{}
};

struct i2c_device_id mpu6050_id_table[] = {
	{ .name = "mpu6050-custom", .driver_data = 123456 },
	{}
};

static struct i2c_driver mpu6050_driver = {
	.probe     = mpu6050_i2c_driver_probe,
	.remove    = mpu6050_i2c_driver_remove,
	.id_table  = mpu6050_id_table,
	.driver    = {
		.name           = "mpu6050-char-device",
		.of_match_table = of_match_ptr(mpu6050dev_dt_match),
	},
};

static int __init mpu6050_driver_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&mpu6050_drv_data.device_num_base,
				  0, MAX_DEVICES, "mpu6050");
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed\n");
		return ret;
	}

	mpu6050_drv_data.class = class_create(THIS_MODULE, "mpu6050_class");
	if (IS_ERR(mpu6050_drv_data.class)) {
		pr_err("class_create failed\n");
		ret = PTR_ERR(mpu6050_drv_data.class);
		unregister_chrdev_region(mpu6050_drv_data.device_num_base,
					 MAX_DEVICES);
		return ret;
	}

	ret = i2c_add_driver(&mpu6050_driver);
	if (ret < 0) {
		class_destroy(mpu6050_drv_data.class);
		unregister_chrdev_region(mpu6050_drv_data.device_num_base,
					 MAX_DEVICES);
		return ret;
	}

	return 0;
}

static void __exit mpu6050_driver_exit(void)
{
	i2c_del_driver(&mpu6050_driver);
	class_destroy(mpu6050_drv_data.class);
	unregister_chrdev_region(mpu6050_drv_data.device_num_base, MAX_DEVICES);
}

module_init(mpu6050_driver_init);
module_exit(mpu6050_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phan Duc Manh");
MODULE_DESCRIPTION("MPU6050 character device driver with sysfs attributes");