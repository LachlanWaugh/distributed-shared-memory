CC=gcc
CFLAGS	=	-Iinclude/ -Wall

SRC_DIR	:=	src
OBJ_DIR	:=	obj

SRC	:=	$(wildcard $(SRC_DIR)/*.c)
OBJ	:=	$(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

.PHONY	:	all
all	:	dsm

$(OBJ_DIR)/%.o:	$(SRC_DIR)/%.c $(DEPEND) | $(OBJ_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

dsm:	$(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY:	clean
clean:
	rm *.o dsm
