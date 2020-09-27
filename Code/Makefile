CC=gcc
CFLAGS=-I.
DEPEND = dsm.h sm.h
OBJECT = dsm.o sm.o

%.o: %.c $(DEPEND)
	$(CC) -c -o $@ $< $(CFLAGS)

dsm: $(OBJECT)
	$(CC) -o $@ $^ $(CFLAGS)