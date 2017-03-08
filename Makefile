HEADERS = Cache.H FileSystem.H Main.H Metadata.H RmtFs.H
OBJECTS = Cache.o FileSystem.o Main.o Metadata.o RmtFs.o 

CC = g++

# CFLAGS += -g -O0
# CFLAGS += -O0 -pg -g -DDEBUG 
CFLAGS += -O3
CFLAGS += -I. -I. -I/usr/include/fuse -Wall -W -D_FILE_OFFSET_BITS=64
LCFLAGS += -lpthread -lfuse -lssh 

TARGET = snapshotfs

all: $(TARGET) $(HEADERS)


$(TARGET): $(OBJECTS) $(HEADERS)
	$(CC) -o $(TARGET) $(CFLAGS) $(OBJECTS) $(LCFLAGS)

clean:
	rm -rf $(TARGET) $(OBJECTS) err lerr gmon.out

.C.o: $(HEADERS) $<
	$(CC) -c $(CFLAGS) $(MODULECOMPILEFLAGS) -o $@ $<

install: all
	cp $(TARGET) /usr/bin/$(TARGET)
