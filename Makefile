all: btff.so

dep:
	gcc -Wall -O3 -fPIC -DPIC -fno-stack-protector -M *.c > .depend

clean:
	rm -rf *.o *.so

install:
	mkdir -p ~/lib
	cp -f btff.so ~/lib/btff.so

btff.so: common.o btff.o libbtff.o 
	ld -shared -o $@ $^ -ldl -lpthread

.c.o:
	gcc -Wall -O3 -fPIC -DPIC -fno-stack-protector -c $<

include .depend
