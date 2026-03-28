# Linux Device Driver — MPU6050 on BeagleBone Black
## Day 1: Deep dive on hardware datasheet & Write device tree

---

## Mục tiêu

Viết một **character device driver** cho cảm biến MPU6050 giao tiếp I2C trên BeagleBone Black (BBB). Đây là giai đoạn chuẩn bị trước khi viết driver code — bao gồm đọc datasheet, tra cứu pin mux, cấu hình Device Tree và verify phần cứng.

---

## Phase 1: Đọc Datasheet MPU6050

### 1.1 Register Map

Tra cứu datasheet gốc InvenSense để nắm các register quan trọng:

| Addr (Hex) | Register Name   | Ý nghĩa                                      |
|------------|-----------------|----------------------------------------------|
| 0x6B       | PWR_MGMT_1      | Wake up device, chọn clock source (SLEEP bit)|
| 0x19       | SMPLRT_DIV      | Sample rate divider                          |
| 0x1A       | CONFIG          | DLPF configuration                           |
| 0x1B       | GYRO_CONFIG     | Full scale range gyroscope                   |
| 0x1C       | ACCEL_CONFIG    | Full scale range accelerometer               |
| 0x3B–0x48  | ACCEL/GYRO OUT  | 14 bytes raw data liên tiếp (6 trục × 2 bytes)|
| 0x75       | WHO_AM_I        | Verify chip identity                         |

### 1.2 Phân tích WHO_AM_I Register (0x75)

Register `0x75` chứa field `WHO_AM_I[6:1]` — 6 bits từ bit6 xuống bit1:

```
bits [6:1] = 1 1 0 1 0 0  ←  phần cố định của I2C address
bit  [0]   = -            ←  không dùng
```

Nhà sản xuất cố ý lưu phần cố định của I2C address vào WHO_AM_I. Đọc register này trả về `0x68` → dùng trong driver để **verify chip còn sống và đúng loại**.

### 1.3 Tìm I2C Address

Tìm trong section **"Serial Interface"** của datasheet — không phải trong bảng register map:

```
Slave address = b110100X  (7-bit)

AD0 pin = GND  →  b1101000 = 0x68
AD0 pin = VCC  →  b1101001 = 0x69
```

**Phân biệt 2 khái niệm đều mang giá trị `0x68`:**

| Khái niệm | Ý nghĩa | Dùng ở đâu |
|---|---|---|
| Register address `0x68` | Địa chỉ register SIGNAL_PATH_RESET bên trong chip | Gửi sau khi đã kết nối I2C |
| I2C bus address `0x68` | Địa chỉ để master tìm thấy slave trên bus | Gửi đầu tiên để chọn slave |

**Ai khai báo I2C address:**

- **Phần cứng (AD0 pin)** — quyết định địa chỉ thực tế
- **Device Tree (`reg = <0x68>`)** — bạn khai báo thủ công cho kernel biết
- **Kernel I2C subsystem** — điền vào `i2c_client->addr`, truyền cho driver
- **Driver** — chỉ dùng `client->addr`, không tự đặt

---

## Phase 2: Tra cứu AM335x TRM & BBB SRM

### 2.1 Phân biệt tài liệu cần dùng

```
BBB SRM  →  biết P9_19 = SCL, P9_20 = SDA (tên pin trên header)
    ↓
AM335x Datasheet (SPRS717)  →  bảng Ball Characteristics
    biết mux mode 0→7 của từng pin
    ↓
AM335x TRM  →  Section 9.3.1.50
    biết cấu trúc bit fields của conf_xxx register
    (MMODE, PUDEN, PUTYPESEL, RXACTIVE, SLEWCTRL)
```

### 2.2 Cấu trúc conf_module_pin Register (TRM Section 9.3.1.50)

Mỗi `conf_xxx` register có cấu trúc bit fields:

```
Bit [1:0]  =  MMODE      →  chọn mux mode 0-7
Bit [3]    =  PUDEN      →  0 = pull enabled,  1 = pull disabled
Bit [4]    =  PUTYPESEL  →  0 = pulldown,       1 = pullup
Bit [5]    =  RXACTIVE   →  0 = input disabled, 1 = input enabled
Bit [6]    =  SLEWCTRL   →  0 = fast,           1 = slow
```

### 2.3 Tìm Pin Mux Mode cho I2C2

Từ bảng **Ball Characteristics** trong AM335x Datasheet:

| PIN | NAME      | MODE0      | MODE2      | MODE3     | MODE7     |
|-----|-----------|------------|------------|-----------|-----------|
| 17  | I2C1_SCL  | spi0_cs0   | I2C1_SCL   | ehrpwm    | gpio0[5]  |
| 18  | I2C1_SDA  | spi0_d1    | I2C1_SDA   | ehrpwm    | gpio0[4]  |
| 19  | I2C2_SCL  | uart1_rtsn | —          | I2C2_SCL  | gpio0[13] |
| 20  | I2C2_SDA  | uart1_ctsn | —          | I2C2_SDA  | gpio0[12] |

**Bài học:** Mode 7 = GPIO không phải quy tắc tuyệt đối — một số pin có GPIO ở mode 3, mode 5 tùy thiết kế SoC. Luôn phải tra datasheet cụ thể.

### 2.4 Verify Pin Config trên BBB đang chạy

```bash
cat /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins
```

Kết quả pin P9_19 (PIN94) và P9_20 (PIN95):

```
pin 94 (PIN94) 44e10978 00000033
pin 95 (PIN95) 44e1097c 00000033
```

Phân tích giá trị `0x33 = 0b00110011`:

```
Bit [1:0] = 11  →  MMODE = 3       ← mode 3 = I2C2 ✓
Bit [3]   = 0   →  PUDEN = 0       ← pull enabled ✓
Bit [4]   = 1   →  PUTYPESEL = 1   ← pullup selected ✓
Bit [5]   = 1   →  RXACTIVE = 1    ← input enabled ✓
```

Đối chiếu với DTS trong `am335x-bone-common.dtsi`:

```dts
i2c2_pins: pinmux_i2c2_pins {
    pinctrl-single,pins = <
        AM33XX_PADCONF(AM335X_PIN_UART1_CTSN, PIN_INPUT_PULLUP, MUX_MODE3)
        AM33XX_PADCONF(AM335X_PIN_UART1_RTSN, PIN_INPUT_PULLUP, MUX_MODE3)
    >;
};
```

`PIN_INPUT_PULLUP + MUX_MODE3` → khớp hoàn toàn với giá trị `0x33` đọc được trên hardware.

---

## Phase 3: Kiểm tra I2C Bus

### 3.1 List các I2C bus đang hoạt động

```bash
i2cdetect -l
```

```
i2c-0   OMAP I2C adapter   ← I2C0: dùng nội bộ cho PMIC, không đụng vào
i2c-2   OMAP I2C adapter   ← I2C2: bus dùng cho MPU6050
```

**Lưu ý:** I2C0 được kernel dùng để giao tiếp với TPS65217 (PMIC), không expose ra header P8/P9.

### 3.2 Cơ chế i2cdetect hoạt động

`i2cdetect -r` gửi **Read probe** đến từng địa chỉ 0x00 → 0x7F:

```
Master gửi: START + [address << 1 | READ]
    ↓
Slave nhận ra địa chỉ mình → kéo SDA = LOW → ACK
    ↓
Master nhận ACK → ghi địa chỉ đó vào bảng kết quả

Không có ACK → ô trống (không có thiết bị)
```

### 3.3 Trường hợp pin chưa đúng mode

Nếu pin chưa được cấu hình đúng mode I2C, dùng `config-pin`:

```bash
config-pin P9_19 i2c
config-pin P9_20 i2c
config-pin -q P9_19   # verify
```

Lưu ý: cách này chỉ tồn tại đến khi reboot. Để persistent phải dùng Device Tree Overlay hoặc ghi vào `/etc/rc.local`.

---

## Phase 4: Cắm MPU6050 & Verify Hardware

### 4.1 Nối dây vật lý

```
MPU6050 Module    →    BBB Header
VCC               →    P9_3 hoặc P9_4  (3.3V)
GND               →    P9_1 hoặc P9_2  (GND)
SDA               →    P9_20           (I2C2_SDA)
SCL               →    P9_19           (I2C2_SCL)
AD0               →    GND             (địa chỉ = 0x68)
```

### 4.2 Scan I2C bus

```bash
i2cdetect -y -r 2
```

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --
```

Thấy `68` tại địa chỉ 0x68 → MPU6050 đã được nhận diện trên bus.

### 4.3 Đọc thẳng register WHO_AM_I

```bash
i2cget -y 2 0x68 0x75
```

```
0x68
```

Trả về `0x68` → chip còn sống, giao tiếp I2C bình thường. Hardware OK hoàn toàn.

**Tại sao phải verify hardware trước khi viết driver?**
Nếu viết driver xong mà probe fail, không biết lỗi do code hay do phần cứng. Verify bằng `i2cdetect` và `i2cget` trước → loại trừ hoàn toàn nguyên nhân phần cứng.

---

## Phase 5: Viết Device Tree & Compile

### 5.1 Cấu trúc DTS đã có sẵn

Trong `arch/arm/boot/dts/am335x-bone-common.dtsi`, `&i2c2` node đã có đầy đủ:

```dts
&i2c2 {
    pinctrl-names = "default";
    pinctrl-0 = <&i2c2_pins>;
    status = "okay";
    clock-frequency = <100000>;
    ...
};
```

Không cần tạo mới — chỉ cần **thêm node con** cho MPU6050 vào bên trong.

### 5.2 Node MPU6050 thêm vào DTS

```dts
mpu6050: mpu6050@68 {
    compatible = "invensense,mpu6050-custom";
    reg = <0x68>;
};
```

**Giải thích từng dòng:**

| Phần | Ý nghĩa |
|---|---|
| `mpu6050@68` | tên node, `@68` = địa chỉ I2C (không có 0x) |
| `compatible` | chuỗi để kernel match với driver — phải khớp chính xác |
| `reg = <0x68>` | địa chỉ I2C của slave trên bus (có 0x trong value) |

**Tại sao dùng `"invensense,mpu6050-custom"` thay vì `"invensense,mpu6050"`?**

Kernel đã có driver `inv-mpu6050-i2c` built-in, match với string `"invensense,mpu6050"`. Dùng string custom → driver built-in không cướp mất device trước khi driver tự viết kịp bind vào.

### 5.3 Compile DTB

```bash
sudo make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabihf- am335x-boneblack.dtb
```

Chỉ compile DTB, không cần compile cả kernel. Output:

```
arch/arm/boot/dts/am335x-boneblack.dtb
```

Copy vào BBB:

```bash
cp am335x-boneblack.dtb /boot/dtbs/5.10.168-ti-r71/am335x-boneblack-uboot-univ.dtb
```

**Lưu ý:** File DTB đúng được xác định từ `uEnv.txt`. BBB đang dùng `enable_uboot_cape_universal=1` nên load file `uboot-univ`, không phải `am335x-boneblack.dtb` thông thường.

### 5.4 Verify sau khi reboot

```bash
# Device node xuất hiện chưa?
ls /sys/bus/i2c/devices/
# Phải thấy: 2-0068

# Driver cũ không còn cướp device nữa?
dmesg | grep -i mpu6050
# Không còn thấy inv-mpu6050-i2c

# Compatible string đúng chưa?
cat /sys/bus/i2c/devices/2-0068/uevent
# MODALIAS chứa string custom của mình
```

---

## Tổng kết — Checklist hoàn thành

| Hạng mục | Kết quả | Trạng thái |
|---|---|---|
| Đọc datasheet tìm I2C address | `b110100X` → 0x68/0x69 | ✓ DONE |
| Hiểu WHO_AM_I register | bits[6:1] = phần cố định I2C addr | ✓ DONE |
| Tra AM335x Datasheet pin mux | I2C2 = MUX_MODE3 trên uart1_ctsn/rtsn | ✓ DONE |
| Đọc TRM cấu trúc conf register | MMODE/PUDEN/PUTYPESEL/RXACTIVE | ✓ DONE |
| Verify pin config trên hardware | `0x33` = mode3 + pullup + input | ✓ DONE |
| I2C bus hoạt động | i2c-2 hiện trong `i2cdetect -l` | ✓ DONE |
| Phần cứng detect được | `0x68` hiện trong `i2cdetect -y -r 2` | ✓ DONE |
| WHO_AM_I verify | `i2cget` trả về `0x68` | ✓ DONE |
| Thêm node vào DTS | `mpu6050@68` trong `&i2c2` | ✓ DONE |
| Compile và flash DTB | `2-0068` xuất hiện sau reboot | ✓ DONE |
| Tránh conflict driver cũ | Dùng compatible string custom | ✓ DONE |

---
## Day 2: Write Character Device Driver

---

## Mục tiêu

Viết hoàn chỉnh một **I2C character device driver** cho MPU6050 — bao gồm thiết kế data structures, probe/remove, file operations (open/read/ioctl) và user space test app.

---

## Phase 1: Thiết kế Data Structures

### 1.1 Phân biệt 2 loại private data

Driver cần 2 struct riêng biệt — tư duy theo **scope**:

```
mpu6050drv_private_data   →   driver-level, tồn tại suốt vòng đời module
                               1 instance duy nhất, khai báo global static

mpu6050dev_private_data   →   per-device, tồn tại từ probe đến remove
                               mỗi MPU6050 trên bus có 1 instance riêng
```

### 1.2 `struct mpu6050drv_private_data`

```c
struct mpu6050drv_private_data {
    int total_devices;       /* đếm số device đang được quản lý */
    dev_t device_num_base;   /* base major:minor number cho toàn driver */
    struct class *class;     /* device class dùng chung /sys/class/mpu6050_class */
};

static struct mpu6050drv_private_data mpu6050_drv_data;
```

Khai báo **global static** vì:
- Khởi tạo trong `module_init()` — trước khi probe chạy
- probe() cần truy cập để lấy `class` và `device_num_base`
- Tồn tại đến khi `module_exit()`

### 1.3 `struct mpu6050dev_private_data`

```c
struct mpu6050dev_private_data {
    struct i2c_client *client;   /* I2C client — dùng trong read/ioctl để giao tiếp hardware */
    dev_t dev_num;               /* major:minor number của device này */
    struct cdev cdev;            /* embedded char device struct */
    char buffer[14];             /* raw data buffer */
    struct mutex lock;           /* bảo vệ I2C transaction */
    struct device *device;       /* device node /dev/mpu6050-X */
};
```

**Tại sao `struct cdev` được embedded thay vì dùng pointer?**

```
container_of(inode->i_cdev, struct mpu6050dev_private_data, cdev)
    ↑
    macro này yêu cầu cdev phải nằm VẬT LÝ trong struct
    nếu dùng pointer → không thể dùng container_of
```

**Tại sao cần lưu `struct i2c_client *client`?**

```
open()   → container_of(inode->i_cdev) → lấy được dev_data
read()   → cần gọi i2c_smbus_read_i2c_block_data(client, ...)
ioctl()  → cần gọi i2c_smbus_write_byte_data(client, ...)
           ↑
           tất cả I2C API đều cần client → phải lưu trong dev_data
```

---

## Phase 2: module_init() và module_exit()

### 2.1 Phân chia trách nhiệm init vs probe

```
module_init()   →   chạy 1 lần khi insmod
                    khởi tạo những thứ dùng chung toàn driver

probe()         →   chạy mỗi khi kernel match được 1 device
                    khởi tạo những thứ per-device
```

**Nếu có 2 con MPU6050 trên 2 bus khác nhau:**
```
module_init() chạy: 1 lần
probe() chạy:       2 lần
```

### 2.2 module_init() — 3 việc theo thứ tự

```
1. alloc_chrdev_region()   →   cấp dải major:minor cho toàn driver
2. class_create()          →   tạo /sys/class/mpu6050_class
3. i2c_add_driver()        →   đăng ký driver với I2C subsystem
                               → kernel bắt đầu gọi probe() khi match device
```

**Error handling — cleanup ngược thứ tự:**

```
alloc_chrdev_region fail  →  return lỗi
class_create fail         →  unregister_chrdev_region → return lỗi
i2c_add_driver fail       →  class_destroy → unregister → return lỗi
```

### 2.3 module_exit() — cleanup ngược thứ tự init

```
i2c_del_driver()              ←  đăng ký sau cùng, hủy trước
class_destroy()
unregister_chrdev_region()    ←  đăng ký đầu tiên, hủy sau cùng
```

---

## Phase 3: probe() — Trái tim của driver

### 3.1 Thứ tự đầy đủ trong probe()

```
1.  of_match_device()          →  verify DT compatible string match
2.  devm_kzalloc()             →  alloc per-device struct
3.  mutex_init()               →  init lock trước khi dùng
4.  i2c_smbus_read_byte_data() →  đọc WHO_AM_I (0x75), verify = 0x68
5.  DEVICE_RESET               →  reset chip về default state
6.  msleep(100)                →  đợi reset hoàn thành
7.  Wake up + CLKSEL           →  ghi PWR_MGMT_1, chọn PLL clock
8.  msleep(10)                 →  đợi PLL stabilize
9.  GYRO_CONFIG                →  set full scale range gyro
10. ACCEL_CONFIG               →  set full scale range accel
11. i2c_set_clientdata()       →  lưu dev_data vào client
12. dev_data->client = client  →  lưu client vào dev_data
13. dev_data->dev_num          →  tính device number = base + total
14. cdev_init() + cdev_add()   →  đăng ký char device
15. device_create()            →  tạo /dev/mpu6050-X
16. total_devices++            →  cập nhật counter
```

### 3.2 Tại sao verify WHO_AM_I?

```
probe() được gọi khi compatible string match
→ nhưng đây chỉ là software match (DTS)
→ hardware thực tế có thể:
    - chip bị lỗi
    - dây nối sai
    - chip giả
→ đọc WHO_AM_I = 0x68 → xác nhận đúng chip vật lý
```

### 3.3 Tại sao cần i2c_set_clientdata()?

```
probe(struct i2c_client *client)
    → alloc dev_data
    → i2c_set_clientdata(client, dev_data)
         ↓
         lưu dev_data vào client->dev.driver_data

remove(struct i2c_client *client)
    → kernel chỉ truyền vào client
    → KHÔNG có dev_data trực tiếp
    → i2c_get_clientdata(client) → lấy lại dev_data
```

**So sánh 2 con đường lấy dev_data:**

| Context | Con đường | API |
|---|---|---|
| `open()` / `read()` / `ioctl()` | `inode->i_cdev` → `container_of` | `container_of` |
| `remove()` | `i2c_client` → `driver_data` | `i2c_get_clientdata()` |

### 3.4 Hardware init — đọc từ datasheet

**PWR_MGMT_1 (0x6B):**
```
Bit 7 = DEVICE_RESET  →  set 1, chip tự clear khi xong
Bit 6 = SLEEP         →  1 = sleep (default sau power-on), 0 = wake up
Bit 0 = CLKSEL[0]     →  001 = PLL with X gyro reference (khuyến nghị)
```

Sequence chuẩn từ datasheet:
```
1. Ghi DEVICE_RESET = 1   →  reset toàn bộ register về default
2. Wait 100ms              →  đợi reset xong (từ datasheet note SPI)
3. Ghi SLEEP=0, CLKSEL=1  →  wake up + chọn clock ổn định hơn
4. Wait 10ms               →  đợi PLL lock
```

**GYRO_CONFIG (0x1B) — bits [4:3] = FS_SEL:**
```
00 = ±250°/s   sensitivity = 131 LSB/°/s
01 = ±500°/s   sensitivity = 65.5 LSB/°/s   ← driver dùng default này
10 = ±1000°/s  sensitivity = 32.8 LSB/°/s
11 = ±2000°/s  sensitivity = 16.4 LSB/°/s
```

**ACCEL_CONFIG (0x1C) — bits [4:3] = AFS_SEL:**
```
00 = ±2g   sensitivity = 16384 LSB/g   ← driver dùng default này
01 = ±4g   sensitivity = 8192  LSB/g
10 = ±8g   sensitivity = 4096  LSB/g
11 = ±16g  sensitivity = 2048  LSB/g
```

### 3.5 Error handling trong probe()

```
Trước cdev_add:
    → chỉ cần return lỗi
    → devm_kzalloc tự cleanup memory

Sau cdev_add, trước device_create:
    → cdev_del() trước khi return

Sau device_create:
    → device_destroy() + cdev_del() trước khi return
```

---

## Phase 4: remove()

### 4.1 Thứ tự cleanup

```
1. i2c_get_clientdata()    →  lấy lại dev_data từ client
2. put chip to sleep       →  best effort, KHÔNG return nếu fail
3. device_destroy()        →  xóa /dev/mpu6050-X
4. cdev_del()              →  hủy đăng ký char device
5. total_devices--         →  cập nhật counter
```

**Nguyên tắc quan trọng trong remove():**

```
Hardware cleanup   →   best effort
                       nếu chip không trả lời → log warning, tiếp tục
                       KHÔNG return sớm

Kernel cleanup     →   bắt buộc hoàn thành
                       device_destroy, cdev_del phải được gọi
                       dù hardware có fail hay không
```

Lý do: chip có thể bị rút dây trước khi rmmod — driver vẫn phải cleanup kernel resources.

---

## Phase 5: File Operations

### 5.1 open()

```
inode->i_cdev
    ↓
container_of(inode->i_cdev, struct mpu6050dev_private_data, cdev)
    ↓
filp->private_data = dev_data   ← lưu lại cho read/write/ioctl dùng
```

`open()` không tương tác hardware — chỉ thiết lập context cho các syscall sau.

### 5.2 read() — đọc 14 bytes raw data

**Thứ tự xử lý:**

```
1. Lấy dev_data từ filp->private_data
2. Kiểm tra count >= sizeof(struct mpu6050_data)
3. mutex_lock_interruptible()     ←  interruptible để Ctrl+C hoạt động
4. i2c_smbus_read_i2c_block_data(client, 0x3B, 14, raw_buffer)
5. Parse 14 bytes → struct mpu6050_data
6. mutex_unlock()
7. copy_to_user()
8. return sizeof(struct mpu6050_data)
```

**Layout 14 bytes từ register 0x3B:**

```
Byte 0,1   →  ACCEL_X high, low   →  combine: (buf[0] << 8) | buf[1]
Byte 2,3   →  ACCEL_Y high, low
Byte 4,5   →  ACCEL_Z high, low
Byte 6,7   →  TEMP high, low
Byte 8,9   →  GYRO_X high, low
Byte 10,11 →  GYRO_Y high, low
Byte 12,13 →  GYRO_Z high, low
```

**Integer scaling — tránh float trong kernel:**

```c
/* Accel: đơn vị mg (milli-g), AFS_SEL=0, sensitivity=16384 LSB/g */
accel_x_mg = (raw_accel_x * 1000) / 16384

/* Gyro: đơn vị mdps (milli-degree/s), FS_SEL=1, sensitivity=65.5 */
gyro_x_mdps = (raw_gyro_x * 10000) / 655   /* 655 = 65.5 × 10 */

/* Temperature: đơn vị centi-celsius */
temp_cc = (raw_temp * 100) / 340 + 3653    /* 3653 = 36.53 × 100 */
```

### 5.3 ioctl() — unlocked_ioctl

**Tại sao dùng `unlocked_ioctl` thay vì `ioctl`?**

```
ioctl cũ         →  giữ Big Kernel Lock suốt quá trình → performance tệ
                    bị remove từ kernel 2.6.36
unlocked_ioctl   →  driver tự quản lý lock riêng → tốt hơn
```

**Validation trước khi xử lý:**

```
_IOC_TYPE(cmd) != MPU6050_MAGIC  →  return -ENOTTY  (sai driver)
_IOC_NR(cmd) > MPU6050_IOC_MAXNR →  return -ENOTTY  (sai command)
```

**Phân biệt cách lấy data theo direction:**

```
_IO commands  (RESET, SLEEP, WAKE_UP):
    arg không phải pointer → không get_user

_IOW commands (SET_GYRO_RANGE, SET_ACCEL_RANGE):
    arg là pointer đến data user muốn gửi xuống
    → get_user(val, (u8 __user *)arg)   ← bên trong từng case

_IOR commands (GET_GYRO_RANGE, GET_ACCEL_RANGE):
    arg là pointer đến buffer user muốn nhận
    → put_user(val, (u8 __user *)arg)   ← bên trong từng case
```

**Helper function `mpu6050_write_reg_bitfield()`:**

```
Vấn đề: nhiều ioctl chỉ muốn thay đổi 1 vài bits trong register
        không muốn ảnh hưởng các bits khác

Giải pháp: read → modify → write
    1. Đọc giá trị hiện tại
    2. Clear bits cần đổi bằng mask: old & ~mask
    3. OR với giá trị mới: | (val & mask)
    4. Chỉ ghi lại nếu giá trị thực sự thay đổi → tối ưu bus I2C
```

---

## Phase 6: UAPI Header — Dùng chung kernel và user space

### 6.1 Vấn đề nếu không có uapi header

```
Không có uapi header:
    User space phải tự define lại tất cả ioctl commands
    → tam sao thất bản
    → user phải tìm hiểu internals của driver
    → dễ sai nếu driver update mà user không cập nhật
```

### 6.2 Cấu trúc file

```
mpu6050_uapi.h   ←  PUBLIC: kernel + user space đều include
    #ifdef __KERNEL__
        #include <linux/ioctl.h>
        #include <linux/types.h>
    #else
        #include <sys/ioctl.h>
        #include <stdint.h>
        typedef uint8_t __u8;
        typedef int32_t __s32;
    #endif
    struct mpu6050_data { ... }
    enum accel_range / gyro_range
    ioctl command defines

mpu6050.h        ←  PRIVATE: chỉ driver dùng
    register addresses
    bit masks / BIT() macros
    internal defines
```

### 6.3 Ioctl command number encoding

`_IOW(magic, nr, type)` encode 4 thông tin vào 32-bit:

```
bits [31:30] = direction   (read/write/none)
bits [29:16] = size        (sizeof type — để kernel verify)
bits [15:8]  = magic       (định danh driver)
bits [7:0]   = nr          (số thứ tự command)
```

---

## Phase 7: Test App

### 7.1 Cấu trúc project

```
mpu6050-driver/
    mpu6050.c          ← kernel driver
    mpu6050.h          ← private header
    mpu6050_uapi.h     ← public header
    Makefile           ← cross compile
    example/
        main.c         ← user space test app
        Makefile       ← cross compile với -static
```

### 7.2 Lý do compile static

```
Host machine:  glibc 2.34 (Ubuntu mới)
BBB runtime:   glibc cũ hơn

Binary linked dynamic → lỗi: GLIBC_2.34 not found
Binary linked static  → tự chứa tất cả lib → chạy được mọi nơi
```

Thêm `-static` vào CFLAGS trong Makefile.

### 7.3 Test sequence

```
1. open("/dev/mpu6050-0", O_RDWR)
2. read() baseline
3. SET_GYRO_RANGE → GET_GYRO_RANGE → verify PASS/FAIL
4. SET_ACCEL_RANGE → GET_ACCEL_RANGE → verify PASS/FAIL
5. RESET → read lại → verify về default
6. SLEEP → WAKE_UP → read lại → verify hoạt động bình thường
7. close()
```

---

## Tổng kết — Checklist Day 2

| Hạng mục | Chi tiết | Trạng thái |
|---|---|---|
| Data structures | drv_data (global) + dev_data (per-device) | ✓ DONE |
| module_init | alloc_chrdev + class_create + i2c_add | ✓ DONE |
| module_exit | cleanup ngược thứ tự init | ✓ DONE |
| probe — WHO_AM_I | verify chip identity qua I2C | ✓ DONE |
| probe — hardware init | reset + wake up + gyro/accel config | ✓ DONE |
| probe — char device | cdev_init + cdev_add + device_create | ✓ DONE |
| i2c_set_clientdata | lưu dev_data để remove() lấy lại | ✓ DONE |
| remove | sleep chip + device_destroy + cdev_del | ✓ DONE |
| open | container_of + filp->private_data | ✓ DONE |
| read | mutex + i2c block read + parse + copy_to_user | ✓ DONE |
| ioctl | validate magic/nr + get_user/put_user + bitfield write | ✓ DONE |
| uapi header | __KERNEL__ guard, dùng chung 2 phía | ✓ DONE |
| test app | 5 test cases, static link, PASS/FAIL report | ✓ DONE |

## Day 3: Interrupt-Driven Architecture & epoll Support

---

## Mục tiêu

Chuyển từ kiến trúc **blocking read** (poll I2C trực tiếp) sang kiến trúc **Event-Driven Architecture (EDA)** — driver chỉ đọc data khi có interrupt từ MPU6050, user space dùng `epoll` để chờ event mà không tốn CPU.

---

## Phase 1: Tại sao cần thay đổi kiến trúc?

### 1.1 Vấn đề với blocking read (Day 2)

```
User space:
    while(1) {
        read(fd, &data, sizeof(data))   ← gọi i2c trực tiếp, block luôn
        process(data)
        sleep(10ms)
    }

Driver read():
    mutex_lock()
    i2c_smbus_read_i2c_block_data()     ← đọc I2C mỗi lần user gọi
    parse data
    copy_to_user()
    mutex_unlock()
```

Vấn đề:
```
CPU liên tục bận dù sensor chưa có data mới
Latency không đảm bảo — user phải tự canh timing
Không scale được — nhiều process cùng đọc gây conflict
```

### 1.2 Kiến trúc EDA với interrupt

```
MPU6050 có data mới → tự kéo INT pin xuống LOW
    ↓
GPIO detect FALLING edge → kernel gọi IRQ handler
    ↓
Driver đọc I2C trong threaded handler → lưu vào dev_data
    ↓
wake_up() → notify user space
    ↓
User space epoll_wait() return → gọi read()
```

Lợi ích:
```
CPU ngủ khi không có data             ← tiết kiệm điện
Latency thấp và đảm bảo              ← hardware driven
User space không cần biết timing      ← chờ event là đủ
```

---

## Phase 2: Hardware — Cấu hình INT pin MPU6050

### 2.1 Chọn GPIO pin trên BBB

Dùng **P9_23** = `gpio1[17]` = GPIO số 49:

```
Tính GPIO number: bank × 32 + pin = 1 × 32 + 17 = 49

Verify trên BBB:
    cat /sys/kernel/debug/gpio | grep 49
    → gpio-49 (P9_23) — không bị claim → free để dùng
```

### 2.2 Khai báo trong Device Tree

```dts
mpu6050: mpu6050@68 {
    compatible = "invensense,mpu6050-custom";
    reg = <0x68>;
    interrupt-parent = <&gpio1>;
    interrupts = <17 IRQ_TYPE_EDGE_FALLING>;
};
```

Giải thích:
```
interrupt-parent = <&gpio1>         ← bank 1 (gpio1)
interrupts = <17 IRQ_TYPE_EDGE_FALLING>
              ↑   ↑
              pin trigger type

IRQ_TYPE_EDGE_FALLING = 0x02
→ trigger khi INT pin chuyển từ HIGH xuống LOW
```

Verify sau khi compile và flash DTB:
```bash
dtc -I fs /sys/firmware/devicetree/base 2>/dev/null | grep -A6 mpu6050@68
# Phải thấy: interrupts = <0x11 0x02>  (0x11=17, 0x02=EDGE_FALLING)
```

### 2.3 Hai register interrupt của MPU6050

**INT_PIN_CFG (0x37) — cấu hình hành vi vật lý:**

| Bit | Tên | Giá trị chọn | Lý do |
|---|---|---|---|
| 7 | INT_LEVEL | 1 = active LOW | khớp EDGE_FALLING |
| 6 | INT_OPEN | 0 = push-pull | không cần external pull-up |
| 5 | LATCH_INT_EN | 0 = pulse 50us | GPIO detect được |
| 4 | INT_RD_CLEAR | 1 = clear khi đọc bất kỳ register | tự động clear |

**INT_ENABLE (0x38) — bật nguồn interrupt:**

| Bit | Tên | Giá trị | Ý nghĩa |
|---|---|---|---|
| 0 | DATA_RDY_EN | 1 | ngắt khi sensor ghi xong data mới |

### 2.4 Tại sao EDGE_FALLING khớp với active LOW?

```
INT_LEVEL = 1  →  chip kéo pin xuống LOW khi có interrupt (active LOW)

Timeline:
HIGH ‾‾‾‾‾|_50us|‾‾‾‾‾
           ↓
        FALLING edge  ← CPU detect tại đây

→ IRQ_TYPE_EDGE_FALLING hoàn toàn phù hợp
```

---

## Phase 3: Thay đổi Data Structures

### 3.1 Thêm vào `struct mpu6050dev_private_data`

```c
struct mpu6050dev_private_data {
    struct i2c_client *client;
    dev_t dev_num;
    struct cdev cdev;
    char buffer[14];
    struct mutex lock;
    wait_queue_head_t read_queue;   ← MỚI: chờ interrupt
    struct device *device;
    int irq_num;                    ← MỚI: lưu IRQ number
    bool data_ready;                ← MỚI: cờ báo có data
    struct mpu6050_data cooked_data; ← MỚI: data đã parse sẵn
};
```

**Tại sao cần `wait_queue_head_t`?**

```
read() cần chờ đến khi có data mới
poll() cần đăng ký với kernel để biết khi nào có data

wait_queue là cơ chế kernel cho phép:
    → process ngủ chờ condition
    → interrupt handler đánh thức process
    → epoll hook vào để nhận notification
```

**Tại sao cần `cooked_data` thay vì đọc trực tiếp trong read()?**

```
Day 2 (blocking):
    read() → gọi I2C → parse → copy_to_user
    ↑ đọc I2C trong file operation context

Day 3 (interrupt-driven):
    interrupt → threaded handler → gọi I2C → parse → lưu cooked_data
    read()    → lấy cooked_data → copy_to_user
    ↑ I2C chỉ được gọi trong interrupt thread context
```

---

## Phase 4: Threaded IRQ — Top Half và Bottom Half

### 4.1 Tại sao cần 2 handler?

```
Hard IRQ context (Top Half):
    KHÔNG được: sleep, mutex_lock, I2C read
    Phải xử lý cực nhanh
    Chỉ làm việc tối thiểu

Process context (Bottom Half / Threaded):
    ĐƯỢC: sleep, mutex_lock, I2C read
    Chạy như kernel thread thông thường
```

### 4.2 Primary handler (Top Half)

```c
static irqreturn_t mpu6050_primary_handler(int irq, void *dev_id) {
    return IRQ_WAKE_THREAD;   ← chỉ đánh thức threaded handler
}
```

`IRQ_WAKE_THREAD` báo kernel: "đánh thức threaded handler, tôi xong rồi".

### 4.3 Threaded handler (Bottom Half)

Thứ tự trong threaded handler:

```
1. mutex_lock()
2. i2c_smbus_read_i2c_block_data() từ register 0x3B, 14 bytes
3. Parse raw bytes → cooked_data
4. mutex_unlock()
5. data_ready = true
6. wake_up_interruptible(&read_queue)
7. return IRQ_HANDLED
```

**Quan trọng: `wake_up` phải nằm NGOÀI mutex:**

```c
mutex_unlock(&dev_data->lock);
dev_data->data_ready = true;        ← ngoài mutex
wake_up_interruptible(&read_queue); ← ngoài mutex
```

Lý do: nếu `wake_up` trong mutex → read() thức dậy → cố lock mutex → mutex vẫn bị handler giữ → unnecessary latency.

### 4.4 `request_threaded_irq` — đăng ký cả 2 handler

```c
ret = request_threaded_irq(
    client->irq,                    ← IRQ number từ Device Tree
    mpu6050_primary_handler,        ← top half
    mpu6050_threaded_handler,       ← bottom half
    IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
    "mpu6050_event",
    dev_data                        ← truyền vào handler qua dev_id
);
```

`IRQF_ONESHOT` — bắt buộc với threaded IRQ: giữ interrupt disabled cho đến khi threaded handler hoàn thành, tránh interrupt storm.

---

## Phase 5: Thay đổi probe() — Thứ tự quan trọng

### 5.1 Thứ tự đúng trong probe()

```
1.  devm_kzalloc()
2.  dev_data->client = client       ← NGAY SAU ALLOC (tránh NULL ptr trong handler)
3.  i2c_set_clientdata()
4.  mutex_init(), init_waitqueue_head(), data_ready = false
5.  WHO_AM_I verify
6.  DEVICE_RESET + msleep(100)
7.  Wake up + CLKSEL + msleep(10)
8.  GYRO_CONFIG, ACCEL_CONFIG
9.  SMPLRT_DIV → set sample rate (10Hz)
10. CONFIG → set DLPF (42Hz bandwidth)
11. INT_PIN_CFG → active LOW, INT_RD_CLEAR
12. INT_ENABLE → DATA_RDY_EN = 1
13. msleep(50)                      ← đợi chip stable trước khi IRQ active
14. request_threaded_irq()          ← CUỐI CÙNG mới đăng ký IRQ
15. cdev_init(), cdev_add()
16. device_create()
17. total_devices++
```

**Tại sao `client` phải gán trước `request_threaded_irq`?**

```
Nếu gán sau:
    request_threaded_irq() → chip ngay lập tức fire interrupt
    threaded handler chạy
    i2c_smbus_read(..., dev_data->client, ...)
                           ↑
                           client = NULL → NULL pointer dereference → CRASH!
```

**Tại sao `request_threaded_irq` phải là cuối cùng (trước cdev)?**

```
INT_ENABLE = 1 → chip bắt đầu generate interrupt liên tục
    ↓
Phải đảm bảo handler đã sẵn sàng hoàn toàn trước khi interrupt đến
    ↓
client, lock, wait_queue phải đã init xong
```

### 5.2 Sample Rate và DLPF

```
SMPLRT_DIV (0x19):
    Sample Rate = Gyro Output Rate / (1 + SMPLRT_DIV)
    Gyro Output Rate = 1000Hz (khi DLPF enabled)
    SMPLRT_DIV = 99 (0x63) → 1000 / (1 + 99) = 10Hz
    → 10 interrupts mỗi giây → phù hợp để học

CONFIG (0x1A) — DLPF:
    0x03 → bandwidth 42Hz → lọc noise tốt hơn
```

### 5.3 Error handling khi fail sau request_threaded_irq

```
cdev_add fail:
    free_irq(dev_data->irq_num, dev_data)   ← phải free IRQ
    return ret

device_create fail:
    free_irq(dev_data->irq_num, dev_data)   ← phải free IRQ
    cdev_del(&dev_data->cdev)
    return ret
```

---

## Phase 6: Thay đổi remove() — Thứ tự cleanup

### 6.1 Thứ tự đúng trong remove()

```
1. i2c_smbus_write_byte_data(INT_ENABLE_REG, 0)  ← disable chip interrupt TRƯỚC
2. sleep chip (best effort, không return nếu fail)
3. free_irq()                                     ← free IRQ sau khi chip đã silent
4. device_destroy()
5. cdev_del()
6. total_devices--
```

**Tại sao disable chip interrupt trước free_irq?**

```
Nếu free_irq trước khi disable chip:
    chip vẫn đang generate interrupt
    → interrupt đến trong khoảng free_irq đang chạy
    → kernel xử lý spurious interrupt
    → không crash nhưng không clean
```

---

## Phase 7: Thay đổi read() — Chờ interrupt thay vì gọi I2C

### 7.1 So sánh 2 kiến trúc

**Day 2 — read() gọi I2C trực tiếp:**
```c
read() {
    mutex_lock()
    i2c_smbus_read_i2c_block_data()   ← gọi I2C ở đây
    parse → copy_to_user
    mutex_unlock()
}
```

**Day 3 — read() chờ interrupt:**
```c
read() {
    /* Hỗ trợ O_NONBLOCK */
    if (O_NONBLOCK && !data_ready)
        return -EAGAIN

    /* Chờ interrupt đánh thức */
    wait_event_interruptible(read_queue, data_ready)

    /* Lấy data đã parse sẵn */
    mutex_lock()
    copy_to_user(cooked_data)
    data_ready = false
    mutex_unlock()
}
```

### 7.2 `wait_event_interruptible` hoạt động như thế nào?

```
wait_event_interruptible(queue, condition):
    if (condition đã true) → return ngay
    else:
        process ngủ trên queue
        khi wake_up() được gọi → check condition lại
        nếu condition true → return 0
        nếu bị signal interrupt → return -ERESTARTSYS
```

### 7.3 O_NONBLOCK support

```c
if ((filp->f_flags & O_NONBLOCK) && !mpu6050dev_data->data_ready)
    return -EAGAIN;
```

Dùng khi user space dùng `EPOLLET` (edge-triggered epoll):
```
Edge-triggered epoll → phải đọc hết data trong vòng lặp
→ cần O_NONBLOCK để biết khi nào hết data (EAGAIN)
```

---

## Phase 8: Implement `.poll` — Cầu nối với epoll

### 8.1 Tại sao cần `.poll`?

```
read() blocking → user space bị block hoàn toàn
    → không thể chờ nhiều fd cùng lúc

poll()/select()/epoll() → user space chờ event không tốn CPU
    → có thể chờ nhiều fd cùng lúc
    → timeout linh hoạt
```

### 8.2 `.poll` làm đúng 2 việc

```c
__poll_t mpu6050_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned int mask = 0;

    /* Việc 1: đăng ký wait_queue với kernel poll mechanism */
    poll_wait(filp, &dev_data->read_queue, wait);

    /* Việc 2: báo cáo trạng thái hiện tại */
    if (dev_data->data_ready)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}
```

**`poll_wait()` không phải là hàm ngủ** — nó nói với kernel:

```
"Nếu process cần chờ, hãy cho nó ngủ trên wait_queue này.
 Khi wake_up() được gọi trên queue này → đánh thức process."
```

### 8.3 Flow hoàn chỉnh epoll ↔ driver

```
User:  epoll_wait(epfd, ...)
           ↓
Kernel gọi .poll() lần 1:
    poll_wait()  ← kernel đăng ký ep_poll_callback vào wait_queue
    data_ready = false → return 0
           ↓
Kernel cho process ngủ
           ↓
MPU6050 INT pin kéo LOW
    → threaded handler đọc I2C
    → data_ready = true
    → wake_up_interruptible(&read_queue)
           ↓
kernel gọi ep_poll_callback() [epoll internal]
    → gọi lại .poll() lần 2
    → data_ready = true → return EPOLLIN
    → thêm fd vào ready list
           ↓
epoll_wait() return
    ↓
User: read(fd, &data, sizeof(data))
```

**Driver không biết epoll tồn tại** — driver chỉ cần:
- Implement `.poll()` với `poll_wait()`
- Gọi `wake_up()` khi có data

Kernel VFS tự kết nối hai phía.

---

## Phase 9: Debug — Interrupt Storm

### 9.1 Nguyên nhân interrupt storm

```
I2C read fail trong threaded handler
    ↓
return IRQ_HANDLED nhưng INT_STATUS chưa được clear
    ↓
MPU6050 vẫn còn pending interrupt
    ↓
GPIO controller thấy signal → fire interrupt lại
    ↓
Handler chạy → fail → không clear → fire lại
    ↓
Vòng lặp vô hạn → RT throttling → board lag
```

### 9.2 Giải pháp — Đọc INT_STATUS trước

```c
/* Đầu threaded handler — clear interrupt TRƯỚC TIÊN */
ret = i2c_smbus_read_byte_data(dev_data->client, MPU6050_INT_STATUS_REG);
if (ret < 0) {
    /* I2C lỗi nhưng interrupt vẫn phải được clear */
    return IRQ_HANDLED;
}

/* Verify đúng là DATA_RDY */
if (!(ret & MPU6050_INT_DATA_RDY))
    return IRQ_HANDLED;

/* Bây giờ mới đọc data */
```

### 9.3 Nguyên nhân NULL pointer dereference

```
Lỗi: dev_data->client = NULL khi threaded handler chạy

Nguyên nhân:
    request_threaded_irq() đăng ký IRQ
    chip ngay lập tức fire interrupt
    threaded handler chạy
    i2c_smbus_read(dev_data->client) ← client chưa được gán!

Fix: gán client TRƯỚC request_threaded_irq()
```

---

## Phase 10: epoll trong User Space

### 10.1 Level-triggered vs Edge-triggered

```
Level-triggered (mặc định):
    epoll_wait báo khi buffer còn data
    → an toàn, không bỏ sót
    → phù hợp với driver MPU6050

Edge-triggered (EPOLLET):
    epoll_wait chỉ báo khi trạng thái thay đổi
    → phải đọc hết data trong vòng lặp
    → cần O_NONBLOCK + EAGAIN
    → phức tạp hơn
```

### 10.2 Sử dụng epoll trong test app

```c
/* Setup epoll */
int epfd = epoll_create1(0);
struct epoll_event ev;
ev.events  = EPOLLIN;          ← level-triggered
ev.data.fd = fd;
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

/* Event loop */
struct epoll_event events[1];
while (1) {
    ret = epoll_wait(epfd, events, 1, 2000);  ← timeout 2s
    if (ret == 0) { printf("timeout\n"); continue; }
    if (ret < 0) { perror("epoll_wait"); break; }

    if (events[0].events & EPOLLIN) {
        read(fd, &data, sizeof(data));
        process(data);
    }
}
```

---

## Tổng kết — Checklist Day 3

| Hạng mục | Chi tiết | Trạng thái |
|---|---|---|
| Chọn GPIO pin | P9_23 = gpio1[17] = GPIO49, free | ✓ DONE |
| Device Tree | interrupt-parent + interrupts trong mpu6050 node | ✓ DONE |
| INT_PIN_CFG | active LOW + INT_RD_CLEAR | ✓ DONE |
| INT_ENABLE | DATA_RDY_EN = 1 | ✓ DONE |
| dev_data thêm fields | wait_queue, irq_num, data_ready, cooked_data | ✓ DONE |
| Primary handler | chỉ return IRQ_WAKE_THREAD | ✓ DONE |
| Threaded handler | mutex + I2C read + parse + wake_up | ✓ DONE |
| Thứ tự probe() | client gán trước request_irq, irq sau hardware config | ✓ DONE |
| SMPLRT_DIV | 10Hz sample rate | ✓ DONE |
| DLPF | 42Hz bandwidth filter | ✓ DONE |
| remove() | disable chip irq → sleep → free_irq → cleanup | ✓ DONE |
| read() | wait_event_interruptible + O_NONBLOCK | ✓ DONE |
| .poll | poll_wait + check data_ready + POLLIN | ✓ DONE |
| Error handling | free_irq khi cdev/device_create fail | ✓ DONE |
| Debug interrupt storm | đọc INT_STATUS đầu handler | ✓ TODO |

---

## Bài học rút ra

```
1. Interrupt context có nhiều hạn chế → dùng threaded IRQ để làm I2C
2. Thứ tự init trong probe() quan trọng → client trước, IRQ cuối
3. Interrupt storm xảy ra khi không clear INT_STATUS
4. wake_up() ngoài mutex → giảm latency
5. .poll() = poll_wait() + bitmask → kernel tự kết nối với epoll
6. Driver không cần biết epoll — chỉ cần wait_queue và wake_up()
```