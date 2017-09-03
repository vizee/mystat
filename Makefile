mystat.exe: mystat.c
	gcc -std=c99 -O3 -Wall -Werror\
		-DUNICODE -D_DEBUG -DUSE_BUFIO\
		-o $@ $^ \
		-lkernel32 -luser32 \
		-Wl,--subsystem,windows,--gc-sections,--strip-all

clean:
	rm mystat.exe
