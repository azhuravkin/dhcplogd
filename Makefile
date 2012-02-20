CC = gcc -Wall -O2
TARGET = dhcplogd
OBJECTS = dhcplogd.o
LIBS = -L/usr/lib/mysql -lmysqlclient -lpcre

all: $(TARGET)

$(TARGET): Makefile $(OBJECTS)
	$(CC) -o $(TARGET) $(OBJECTS) $(LIBS)

dhcplogd.o: Makefile dhcplogd.c
	$(CC) -D'PROG_NAME="$(TARGET)"' -o dhcplogd.o -c dhcplogd.c

install: $(TARGET)
	install -s $(TARGET) /usr/local/sbin/$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)
