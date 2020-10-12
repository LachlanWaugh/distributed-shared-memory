CC=gcc
CFLAGS=-I.
DEPEND = dsm.h sm.h allocator.h
OBJECT = dsm.o sm.o allocator.o

%.o: %.c $(DEPEND)
	$(CC) -c -o $@ $< $(CFLAGS)

dsm: $(OBJECT)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm *.o dsm