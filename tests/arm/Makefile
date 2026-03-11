CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -nostdlib -ffreestanding
LDFLAGS = -T test.ld -nostdlib

TESTS = test_basic test_alu test_thumb32 test_mem test_vfp test_it test_exceptions test_peripherals

all: $(addsuffix .bin, $(TESTS))

%.o: %.s
	$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
