CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding
LDFLAGS = -T test.ld -nostdlib

TESTS = test_basic

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
