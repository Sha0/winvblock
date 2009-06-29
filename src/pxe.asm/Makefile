all: aoe.0

clean:
	rm -rf obj

obj/aoe.o: aoe.S aoe.h Makefile
	@rm -rf aoe.0
	@mkdir -p obj
	gcc -Wall -c -o obj/aoe.o aoe.S

obj/pxe.o: pxe.S aoe.h Makefile
	@rm -rf aoe.0
	@mkdir -p obj
	gcc -Wall -c -o obj/pxe.o pxe.S

obj/int13.o: int13.S aoe.h Makefile
	@rm -rf aoe.0
	@mkdir -p obj
	gcc -Wall -c -o obj/int13.o int13.S

obj/lib.o: lib.S aoe.h Makefile
	@rm -rf aoe.0
	@mkdir -p obj
	gcc -Wall -c -o obj/lib.o lib.S

obj/global.o: global.S aoe.h Makefile
	@rm -rf aoe.0
	@mkdir -p obj
	gcc -Wall -c -o obj/global.o global.S

obj/debug.o: debug.S aoe.h Makefile
	@rm -rf aoe.0
	@mkdir -p obj
	gcc -Wall -c -o obj/debug.o debug.S

aoe.0: aoe.ld obj/aoe.o obj/pxe.o obj/int13.o obj/lib.o obj/global.o obj/debug.o Makefile
	@rm -rf aoe.0
	ld -static -T aoe.ld obj/aoe.o obj/pxe.o obj/int13.o obj/lib.o obj/global.o obj/debug.o -o obj/aoe
	objcopy -O binary obj/aoe aoe.0
	@if [ `expr \`find aoe.0 -printf "%s"\` % 2` == 0 ]; then echo -en "\0" >> aoe.0; fi
	@find aoe.0 -printf "%f size: %s\n"
