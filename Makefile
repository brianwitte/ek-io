CFLAGS := -std=c11 -Wall -Wextra -pedantic -Werror -Wshadow \
          -Wstrict-prototypes -Wmissing-prototypes -Wconversion \
          -Wdouble-promotion -Wformat=2 -Wnull-dereference \
          -Wimplicit-fallthrough -Wmissing-declarations \
          -Wno-unused-parameter -fstack-protector-strong \
          -D_FORTIFY_SOURCE=2 -O2 -g3 -march=native \
          -Iinclude  # Add include directory to search path

LDFLAGS := -fPIE

UNAME_S := $(shell uname)
ifeq ($(shell $(CC) -v 2>&1 | grep -c clang), 1)
    # Clang-specific flags
    CFLAGS += -Weverything -Wno-padded -Wno-poison-system-directories
    # macOS does not support -pie or -z relro, -z now
    ifneq ($(UNAME_S), Darwin)
        LDFLAGS += -pie -Wl,-z,relro,-z,now
    endif
else
    # GCC-specific flags
    CFLAGS += -Wduplicated-cond -Wlogical-op -fstack-clash-protection
    LDFLAGS += -pie -Wl,-z,relro,-z,now
endif

ifeq ($(UNAME_S), Linux)
    SRC := src/ek_fifo.c src/ek_epoll.c
else
    SRC := src/ek_fifo.c src/ek_kqueue.c
endif

SRC += test/ek_test.c

OBJ := $(SRC:.c=.o)
EXEC := ek_test

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(EXEC) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC)

.PHONY: all clean
