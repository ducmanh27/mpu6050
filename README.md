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

## Day 2: WRite Driver Code
---

## Mục tiêu



---
```
Bước 1 — Skeleton:     include headers, module_init/exit
Bước 2 — i2c_driver:   of_device_id, i2c_device_id, probe/remove
Bước 3 — Probe:        verify WHO_AM_I, alloc struct, init hardware
Bước 4 — Char device:  alloc dev_t, cdev_init, class, device node
Bước 5 — File ops:     open, read (14 bytes raw data), ioctl, release
```
