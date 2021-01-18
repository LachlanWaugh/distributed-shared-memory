CC=gcc
CFLAGS=-I -g.
DEPEND = dsm.h sm.h sm_setup.h sm_message.h
OBJECT = dsm.o sm.o sm_setup.o sm_message.o

all : dsm
.PHONY : all

%.o: %.c $(DEPEND)
	$(CC) -c -o $@ $< $(CFLAGS)

dsm: $(OBJECT)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm *.o dsm