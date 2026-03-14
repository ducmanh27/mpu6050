CC          = arm-none-linux-gnueabihf-gcc
CFLAGS = -Wall -Wextra -I.. -static
TARGET      = test_mpu6050
SRC         = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

# Copy lên BBB qua SCP
BBB_IP      ?= 192.168.6.2
BBB_USER    ?= root
BBB_PATH    ?= /home/debian

deploy: $(TARGET)
	scp $(TARGET) $(BBB_USER)@$(BBB_IP):$(BBB_PATH)/

.PHONY: all clean deploy