INCLUDES := $(shell echo | cpp -v 2>&1 | sed -n '/\#include "..." search starts here:/,/End of search list./p' | grep "^ " | sed "s/^ \(.*\)$$/-I\1\/ddk/" | tr "\n" " " | sed "s/ $$//" | sed ":start;s/\/[^\/]*\/\.\.\//\//;t start")

# Next line is duplicated in config.bat, edit both when adding files.
c := driver.c registry.c bus.c disk.c aoe.c protocol.c debug.c
h := driver.h aoe.h protocol.h mount.h portable.h

# This is also duplicated in config.bat.
# The c style aoe.0 is not yet stable enough to use.
PXESTYLE := asm
#PXESTYLE := c

all: bin/aoe.0 bin/loader32.exe bin/aoe32.sys bin/aoe.exe bin/aoe.inf bin/txtsetup.oem

clean:
	@rm -rf src/obj src/pxe.asm/obj src/pxe.c/obj bin

dist:
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makedist.bat"

free: bin/aoe.inf bin/txtsetup.oem $(addprefix src/,$c $h) Makefile
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makefree.bat"
	@touch -r Makefile $(wildcard bin/*.sys)
	
checked: bin/aoe.inf bin/txtsetup.oem $(addprefix src/,$c $h) Makefile
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makechecked.bat"
	@touch -r Makefile $(wildcard bin/*.sys)

bin/aoe.0: src/pxe.$(PXESTYLE)/aoe.0 Makefile
	@mkdir -p bin
	cp src/pxe.$(PXESTYLE)/aoe.0 bin

src/pxe.$(PXESTYLE)/aoe.0: $(wildcard src/pxe.$(PXESTYLE)/*.c) $(wildcard src/pxe.$(PXESTYLE)/*.h) $(wildcard src/pxe.$(PXESTYLE)/*.S) src/pxe.$(PXESTYLE)/aoe.ld src/pxe.$(PXESTYLE)/Makefile Makefile
	rm -rf src/pxe.$(PXESTYLE)/aoe.0
	make -C src/pxe.$(PXESTYLE)

bin/aoe.inf bin/txtsetup.oem: makeinf.bat Makefile
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makeinf.bat ; exit 0" >/dev/null 2>&1
	touch bin/aoe.inf
	touch bin/txtsetup.oem

src/obj/loader32.o: src/loader.c src/portable.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/loader32.o bin/loader32.exe bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/loader.c -o src/obj/loader32.o

bin/loader32.exe: src/obj/loader32.o Makefile
	@mkdir -p bin
	@rm -rf bin/loader32.exe bin/loader64.exe
	gcc $(INCLUDES) -Wall src/obj/loader32.o -o bin/loader32.exe -lsetupapi
	strip bin/loader32.exe

src/obj/mount.o: src/mount.c src/portable.h src/mount.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/mount.o bin/aoe.exe
	gcc -Wall -c src/mount.c -o src/obj/mount.o

bin/aoe.exe: src/obj/mount.o Makefile
	@mkdir -p bin
	@rm -rf bin/aoe.exe
	gcc -Wall src/obj/mount.o -o bin/aoe.exe
	strip bin/aoe.exe

src/obj/driver.o: src/driver.c src/portable.h src/driver.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/driver.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/driver.c -o src/obj/driver.o

src/obj/registry.o: src/registry.c src/portable.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/registry.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/registry.c -o src/obj/registry.o

src/obj/bus.o: src/bus.c src/portable.h src/driver.h src/aoe.h src/mount.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/bus.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/bus.c -o src/obj/bus.o

src/obj/disk.o: src/disk.c src/portable.h src/driver.h src/aoe.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/disk.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/disk.c -o src/obj/disk.o

src/obj/aoe.o: src/aoe.c src/portable.h src/driver.h src/aoe.h src/protocol.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoe.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/aoe.c -o src/obj/aoe.o

src/obj/protocol.o: src/protocol.c src/portable.h src/driver.h src/aoe.h src/protocol.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/protocol.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/protocol.c -o src/obj/protocol.o

src/obj/debug.o: src/debug.c src/portable.h src/driver.h src/mount.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/debug.o src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/debug.c -o src/obj/debug.o

src/obj/aoe.tmp: src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/disk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoe.tmp src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	@gcc -Wall src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/disk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o -Wl,--base-file,src/obj/aoe.tmp -Wl,--entry,_DriverEntry@8 -nostartfiles -nostdlib -lntoskrnl -lhal -lndis -o null
	@rm -rf null.exe

src/obj/aoe.exp: src/obj/aoe.tmp Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoe.exp bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	@dlltool --dllname aoe32.sys --base-file src/obj/aoe.tmp --output-exp src/obj/aoe.exp

bin/aoe32.sys: src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/disk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o src/obj/aoe.exp Makefile
	@mkdir -p bin
	@rm -rf bin/aoe32.sys bin/aoe64.sys bin/aoe32.pdb bin/aoe64.pdb bin/loader64.exe
	@gcc -Wall src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/disk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o -Wl,--subsystem,native -Wl,--entry,_DriverEntry@8 -Wl,src/obj/aoe.exp -mdll -nostartfiles -nostdlib -lntoskrnl -lhal -lndis -o bin/aoe32.sys
#	strip bin/aoe32.sys
