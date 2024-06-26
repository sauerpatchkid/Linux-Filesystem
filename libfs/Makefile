CC	= gcc
#FLAGS	:= -Wall -Wextra -Werror

# Target library
LIB := libfs.a
OBJ := disk.o fs.o

all: $(LIB)

%.o: %.c
	$(CC) -c $< -o $@

$(LIB): $(OBJ)
	ar rcs $@ $^

clean:
	rm -f $(LIB) $(OBJ)