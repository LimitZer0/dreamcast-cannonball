# QRLeaderboard setup disc (KallistiOS). Build with ./build.sh.
TARGET = setup.elf
OBJS = setup.o
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
