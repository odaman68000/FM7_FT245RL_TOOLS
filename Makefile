TARGET = ft245tools
OBJS = main.o io.o util.o bubemul.o d77emul.o d77filesystem.o
CFLAGS = -O

all : $(TARGET)

$(TARGET) : $(OBJS)
	cc -o $(TARGET) $(OBJS) -lc
	strip $(TARGET)

main.o : main.c common.h

io.o : io.c common.h

util.o : util.c common.h

bubemul.o : bubemul.c common.h

d77emul.o : d77emul.c common.h

d77filesystem.o : d77filesystem.c common.h

clean :
	rm -f $(OBJS)
	rm -f $(TARGET)
