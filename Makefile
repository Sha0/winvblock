INCLUDES := $(shell echo | cpp -v 2>&1 | sed -n '/\#include "..." search starts here:/,/End of search list./p' | grep "^ " | sed "s/^ \(.*\)$$/-I\1\/ddk/" | tr "\n" " " | sed "s/ $$//" | sed ":start;s/\/[^\/]*\/\.\.\//\//;t start")

# Next line is duplicated in config.bat, edit both when adding files.
c := driver.c registry.c bus.c aoedisk.c aoe.c protocol.c debug.c probe.c winvblock.rc
h := driver.h aoe.h protocol.h mount.h portable.h

# This is also duplicated in config.bat.
# The c style aoe.0 is not yet stable enough to use.
PXESTYLE := asm
#PXESTYLE := c

all: bin/aoe.0 bin/loader32.exe bin/wvblk32.sys bin/aoe.exe bin/winvblk.inf bin/txtsetup.oem

clean:
	@rm -rf src/obj src/nbp/pxe.asm/obj src/nbp/pxe.c/obj bin

dist:
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makedist.bat"

free: bin/winvblk.inf bin/txtsetup.oem $(addprefix src/,$c $h) Makefile
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makefree.bat"
	@touch -r Makefile $(wildcard bin/*.sys)
	
checked: bin/winvblk.inf bin/txtsetup.oem $(addprefix src/,$c $h) Makefile
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makechecked.bat"
	@touch -r Makefile $(wildcard bin/*.sys)

bin/aoe.0: src/nbp/pxe.$(PXESTYLE)/aoe.0 Makefile
	@mkdir -p bin
	cp src/nbp/pxe.$(PXESTYLE)/aoe.0 bin

src/nbp/pxe.$(PXESTYLE)/aoe.0: $(wildcard src/nbp/pxe.$(PXESTYLE)/*.c) $(wildcard src/nbp/pxe.$(PXESTYLE)/*.h) $(wildcard src/nbp/pxe.$(PXESTYLE)/*.S) src/nbp/pxe.$(PXESTYLE)/aoe.ld src/nbp/pxe.$(PXESTYLE)/Makefile Makefile
	rm -rf src/nbp/pxe.$(PXESTYLE)/aoe.0
	make -C src/nbp/pxe.$(PXESTYLE)

bin/winvblk.inf bin/txtsetup.oem: makeinf.bat Makefile
	@sh -c "unset \`set | cut -f 1 -d \"=\" | egrep -v \"PATH|COMSPEC\"\` 2> /dev/null ; cmd /c makeinf.bat ; exit 0" >/dev/null 2>&1
	touch bin/winvblk.inf
	touch bin/txtsetup.oem

src/loader/obj/loader32.o: src/loader/loader.c src/portable.h Makefile
	@mkdir -p src/loader/obj
	@rm -rf src/loader/obj/loader32.o bin/loader32.exe bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/loader/loader.c -o src/loader/obj/loader32.o

bin/loader32.exe: src/loader/obj/loader32.o Makefile
	@mkdir -p bin
	@rm -rf bin/loader32.exe bin/loader64.exe
	gcc $(INCLUDES) -Wall src/loader/obj/loader32.o -o bin/loader32.exe -lsetupapi
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
	@rm -rf src/obj/driver.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/driver.c -o src/obj/driver.o

src/obj/registry.o: src/registry.c src/portable.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/registry.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/registry.c -o src/obj/registry.o

src/obj/bus.o: src/bus.c src/portable.h src/driver.h src/aoe.h src/mount.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/bus.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/bus.c -o src/obj/bus.o

src/obj/aoedisk.o: src/aoedisk.c src/portable.h src/driver.h src/aoe.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoedisk.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/aoedisk.c -o src/obj/aoedisk.o

src/obj/aoe.o: src/aoe.c src/portable.h src/driver.h src/aoe.h src/protocol.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoe.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/aoe.c -o src/obj/aoe.o

src/obj/protocol.o: src/protocol.c src/portable.h src/driver.h src/aoe.h src/protocol.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/protocol.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/protocol.c -o src/obj/protocol.o

src/obj/debug.o: src/debug.c src/portable.h src/driver.h src/mount.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/debug.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/debug.c -o src/obj/debug.o

src/obj/probe.o: src/probe.c src/portable.h src/driver.h src/mount.h Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/probe.o src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	gcc $(INCLUDES) -c -Wall src/probe.c -o src/obj/probe.o

src/obj/aoe.tmp: src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/aoedisk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o probe.o Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoe.tmp src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	@gcc -Wall src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/aoedisk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o src/obj/probe.o -Wl,--base-file,src/obj/aoe.tmp -Wl,--entry,_DriverEntry@8 -nostartfiles -nostdlib -lntoskrnl -lhal -lndis -o null
	@rm -rf null.exe

src/obj/aoe.exp: src/obj/aoe.tmp Makefile
	@mkdir -p src/obj
	@rm -rf src/obj/aoe.exp bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	@dlltool --dllname wvblk32.sys --base-file src/obj/aoe.tmp --output-exp src/obj/aoe.exp

bin/wvblk32.sys: src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/aoedisk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o src/obj/probe.o src/obj/aoe.exp Makefile
	@mkdir -p bin
	@rm -rf bin/wvblk32.sys bin/wvblk64.sys bin/wvblk32.pdb bin/wvblk64.pdb bin/loader64.exe
	@gcc -Wall src/obj/driver.o src/obj/registry.o src/obj/bus.o src/obj/aoedisk.o src/obj/aoe.o src/obj/protocol.o src/obj/debug.o src/obj/probe.o -Wl,--subsystem,native -Wl,--entry,_DriverEntry@8 -Wl,src/obj/aoe.exp -mdll -nostartfiles -nostdlib -lntoskrnl -lhal -lndis -o bin/wvblk32.sys
#	strip bin/wvblk32.sys
