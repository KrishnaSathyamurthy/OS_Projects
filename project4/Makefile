CC=gcc
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64
# Use this CFLAGS when debugging
# CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64 -DDEBUG_TL
LDFLAGS=-lfuse

OBJ=rufs.o block.o

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

rufs: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o rufs

.PHONY: clean
clean:
	rm -f *.o rufs
