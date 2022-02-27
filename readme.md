20110422

This is a very simple, and not entirely complete tool, that may aid in the
relocation of a GROM cartridge file from one address to another. I used it
to relocate the Editor/Assembler cartridge from >6000 to >2000 successfully.

To use it, you WILL need knowledge of GPL and C, as you WILL need to modify
and rebuild the converter tool. You SHOULD have a GPL dissassembly of the
target program - it will make life easier.

The main things you need to change:

```
// there are some hard-coded assumptions about this relocation range!
int nSrc = 0x6000;         // source offset
int nDest = 0x2000;        // dest offset
```

This may need changing for whatever your needs are.

In handle_call(int &offset) is the real meat, though. This function needs
to know about all CALL'd subroutines that require data after the call,
so that it doesn't try to interpret the data as GPL. Right now, it has the
ones that the Editor/Assembler cart uses, other carts may use other calls!

Likely, converting like this is trial and error. You will try, and if it
does succeed, run the GROM and try to exercise any code path. When it crashes,
then you have to try and figure out what the program missed.

The program attempts to follow all execution paths, but it can be confused.
I had to make some concessions. For instance, it assumes that all CALLs will
return, and that they clear the condition bit. This is not necessarily so!

At any rate, it will emit what it figures out as it goes. It expects a
cartridge image, with the first byte 0xAA, and then parses the standard
header to find the entry points.

To run, just pass the source file, and the destination file (the rest of the
settings are hard coded as noted above.)

gromrelocate inputG.bin outputG.bin

I don't intend to give a lot of support to this program, it's a hacky little
tool. But it may be useful to others! So it's released. Enjoy!




