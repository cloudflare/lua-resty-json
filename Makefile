SRC := mempool.c scaner.c parse_array.c parse_hashtab.c parser.c
OBJ := $(SRC:.c=.o)

CFLAGS := -Wall -O3 -flto -g -MMD -fPIC -fvisibility=hidden #-DDEBUG

all : libljson.so a.out

-include dep.txt

${OBJ} : %.o : %.c
	$(CC) $(CFLAGS) -DBUILDING_SO -c $<

libljson.so : ${OBJ}
	$(CC) $(CFLAGS) -DBUILDING_SO $^ -shared -o $@
	cat *.d > dep.txt

a.out : libljson.so demo.o
	$(CC) $(CFLAGS) -Wl,-rpath=./ demo.o -L. -lljson -o $@

test :
	$(MAKE) -C tests

clean:; rm -f *.o *.so a.out *.d dep.txt

