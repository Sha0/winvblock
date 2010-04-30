CFLAGS := -O0
CFLAGS += -nostdlib -fleading-underscore
#CFLAGS += -DMINIMAL
CFLAGS += -DSERIAL
LDFLAGS := -nostdlib -static -T aoe.ld -s

all: aoe.0

clean:
	rm -rf obj sizes.txt

obj/asm.o: asm.S Makefile
	@rm -rf obj/asm.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -c -o obj/asm.o asm.S

obj/debug.o: debug.S Makefile
	@rm -rf obj/debug.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -c -o obj/debug.o debug.S

obj/main.s: main.c main.h asm.h pxe.h lib.h Makefile
	@rm -rf obj/main.s obj/main.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -S -o obj/main.s main.c

obj/main.o: obj/main.s Makefile
	@rm -rf obj/main.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -c -o obj/main.o obj/main.s

obj/pxe.s: pxe.c pxe.h asm.h Makefile
	@rm -rf obj/pxe.s obj/pxe.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -S -o obj/pxe.s pxe.c

obj/pxe.o: obj/pxe.s Makefile
	@rm -rf obj/pxe.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -c -o obj/pxe.o obj/pxe.s

obj/libasm.o: libasm.S Makefile
	@rm -rf obj/libasm.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -c -o obj/libasm.o libasm.S

obj/lib.s: lib.c lib.h asm.h printf.c Makefile
	@rm -rf obj/lib.s obj/lib.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -S -o obj/lib.s lib.c

obj/lib.o: obj/lib.s Makefile
	@rm -rf obj/lib.o aoe.0
	@mkdir -p obj
	gcc $(CFLAGS) -Wall -c -o obj/lib.o obj/lib.s

aoe.0: aoe.ld obj/asm.o obj/debug.o obj/main.o obj/pxe.o obj/libasm.o obj/lib.o Makefile
	@rm -rf aoe.0
	ld $(LDFLAGS) obj/asm.o obj/debug.o obj/main.o obj/pxe.o obj/libasm.o obj/lib.o -o obj/aoe
	objcopy -O binary obj/aoe aoe.0
	@if [ `expr \`find aoe.0 -printf "%s"\` % 2` == 0 ]; then echo -en "\0" >> aoe.0; fi
	@find aoe.0 -printf "%f size: %s\n"
ifneq ($(findstring msys,$(MACHTYPE)), msys)
	@echo "readelf -s *.o | sort -nr > sizes.txt"
	@readelf -s *.o | egrep "FUNC|OBJECT" | tr -s " " | cut -f 4,9 -d " " | sort -nr > sizes.txt
else
	@rm -rf sizes.txt
endif
