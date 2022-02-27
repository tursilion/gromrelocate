//
// (C) 2011 Mike Brent aka Tursi aka HarmlessLion.com
// This software is provided AS-IS. No warranty
// express or implied is provided.
//
// This notice defines the entire license for this software.
// All rights not explicity granted here are reserved by the
// author.
//
// You may redistribute this software provided the original
// archive is UNCHANGED and a link back to my web page,
// http://harmlesslion.com, is provided as the author's site.
// It is acceptable to link directly to a subpage at harmlesslion.com
// provided that page offers a URL for that purpose
//
// Source code, if available, is provided for educational purposes
// only. You are welcome to read it, learn from it, mock
// it, and hack it up - for your own use only.
//
// Please contact me before distributing derived works or
// ports so that we may work out terms. I don't mind people
// using my code but it's been outright stolen before. In all
// cases the code must maintain credit to the original author(s).
//
// Unless you have explicit written permission from me in advance,
// this code may never be used in any situation that changes these
// license terms. For instance, you may never include GPL code in
// this project because that will change all the code to be GPL.
// You may not remove these terms or any part of this comment
// block or text file from any derived work.
//
// -COMMERCIAL USE- Contact me first. I didn't make
// any money off it - why should you? ;) If you just learned
// something from this, then go ahead. If you just pinched
// a routine or two, let me know, I'll probably just ask
// for credit. If you want to derive a commercial tool
// or use large portions, we need to talk. ;)
//
// Commercial use means ANY distribution for payment, whether or
// not for profit.
//
// If this, itself, is a derived work from someone else's code,
// then their original copyrights and licenses are left intact
// and in full force.
//
// http://harmlesslion.com - visit the web page for contact info
//

// gromrelocate.cpp : Defines the entry point for the console application.
// Can't handle tables with addresses in them - do these by hand. sorry!
// CAN handle the standard header, though.

#include "stdafx.h"
#include <stdio.h>

FILE *fp;

int AreasToLook[65536];		// list of pointers to examine for code paths
bool Tested[65536];
unsigned char rom[65536];
int RomSize;
int nAreas;

// there are some hard-coded assumptions about this relocation range!
int nSrc = 0x6000;			// source offset
int nDest = 0x2000;			// dest offset

// with these, we can check if the condition flag has been tested both ways,
// resulting in an unconditional branch even though it's a conditional statement.
// then we know to end the thread.
int cond = 0;
#define COND_BS 0x01
#define COND_BR 0x02
#define COND_SET (COND_BS|COND_BR)

// array of byte opcodes to functions to handle them
bool (*fctns[256])(int&);

int getword(int offset) {
	if (offset >= nSrc) offset-=nSrc;
	return rom[offset]*256+rom[offset+1];
}

void patchword(int offset) {
	if (Tested[offset]) return;		// don't patch it twice!
	Tested[offset]=true;			// we're a bit spotty since we skip over arguments
	if (offset >= nSrc) offset-=nSrc;
	if (getword(offset) >= nSrc) {
		int o = getword(offset);
		int n = o - nSrc + nDest;
		rom[offset] = n/256;
		rom[offset+1]=n%256;
		printf("    (%04X:) >%04X -> >%04X\n", offset, o, getword(offset));
	} else if (getword(offset) >= nDest) {
		printf("    -->> WARNING: Value >%04X at offset >%04X is in target range!\n", getword(offset), offset);
	}
}

// not actually called, this one, just a flag
bool endpath(int &offset) {
	return false;
}

// this function doesn't do anything
bool skip0(int &offset) {
	return true;
}

// skip the argument
bool skip1(int &offset) {
	offset++;
	return true;
}

// absolute branch to any GROM location
bool handle_b(int &offset) {
	// we follow this one - note that a B to the console GROM
	// will not patch and will break this program

	// condition bit is zeroed, so we pretend BS was already called
	cond = COND_BR;

	if (getword(offset) > nSrc) {
		patchword(offset);
		offset = getword(offset) - nDest;	// new location
		if (offset < 0) {
			printf("Branched out of range.\n");
			return false;
		}
	} else {
		return false;
	}
	return true;
}

// absolute call, we assume it will return!
// save the subroutine to look at later
bool handle_call(int &offset) {
	// reset cond as best guess - the problem is that call can return a condition bit if RTNC was used
	cond = 0;

	// we need to determine if the called subroutine returns a condition bit or not!
	int x=getword(offset);
	if (x > nSrc) {
		patchword(offset);
		offset+=2;
		AreasToLook[nAreas++] = x;

		// check for known local functions needing data
		switch (x) {
			// editor/assembler only!
			case 0x69d0:	// emit text
			case 0x69c4:	// warning
			case 0x65b2:	// prepare PAB
				// relocate address
				patchword(offset);
				offset+=2;
				break;
		}
	} else {
		// check for known GROM 0 routines that take a data argument
		offset+=2;
		switch (x) {
			case 0x0010:	// dsrlnk - one byte
			case 0x14a9:	// write text (cs1?)
			case 0x1499:	// more text (cs1?)
			case 0x149f:	// more text (cs1?)
			case 0x14fe:	// more text (cs1?)
				offset++;
				break;
			
			case 0x001c:	// error routine - takes an argument
				patchword(offset);
				offset+=2;
				break;
		}
	}

	return true;
}

// FMT is a complex beast, we just need to try and find the end.
// there is a 'LOOP' opcode that takes an address
bool handle_fmt(int &offset) {
	int nNestRpt = 0;
	char buf[64];

/*
	FMT format jump table
	0CDC 050A 0,1 Horizontal string projection
	0CDE 0508 2,3 Vertical string projection
	0CE0 0504 4,5 Repeat horizontal character
	0CE2 0502 6,7 Repeat vertical character
	0CE4 0534 8,9 Relative fixed row
	0CE6 0532 A,B Relative fixed column
	0CE8 053A C,D Loop values
	0CEA 056C E,F Fixed position row and column, screen offset
*/

	printf("Handling FMT at >%04x\n", offset);

	// for the most part, the code is the high 3 bits
	for (;;) {
		int x = rom[offset++];
		switch (x&0xe0) {
			case 0x00:	// HTEX - opcode is the number of bytes + 1
			case 0x20:	// VTEX - same, but vertical
				x&=0x1f;
				x++;
				memset(buf, 0, sizeof(buf));
				memcpy(buf, &rom[offset], x);
				printf("\tTEXT %d,'%s'\n", x, buf);
				offset+=x;
				break;

			case 0x40:	// HCHAR - opcode is count, plus char
			case 0x60:	// VCHAR - opcode is count, plus char
				printf("\tV/HCHAR %d,%d\n", x&0x1f+1, rom[offset]);
				offset++;
				break;

			case 0x80:	// COL+ - opcode is how much (we don't care here)
			case 0xa0:	// ROW+ - opcode is how much 
				printf("\tC/ROW+ %d\n", x&0x1f+1);
				break;

			case 0xc0:	// RPT - opcode is loop count
				printf("\tRPT %d\n", x&0x1f+1);
				nNestRpt++;	// affects the 0xfb opcode
				break;

			case 0xe0:	// this one needs to be broken up a bit more
				switch (x) {
					case 0xfb:	
						if (nNestRpt) {
							// LOOP (includes an address which we may need to patch??? It's two full bytes, anyway)							
							nNestRpt--;
							x=getword(offset);
							printf("\tLOOP >%04X\n", x);
							if (x>nSrc) patchword(offset);
							offset+=2;
						} else {
							// else it's FEND - we're done
							printf("\tFEND\nExitting FMT at >%04x\n", offset);
							return true;
						}
						break;

					case 0xfc:	// SCRO (defined)
						printf("\tSCRO >%02X\n", x&0x1f);
						break;

					case 0xfd:	// SCRO (with value)
					case 0xfe:	// set ROW
					case 0xff:	// set COL
						printf("\tSCRO/ROW/COL >%02X\n", rom[offset]);
						offset++;
						break;

					default:		// HTEX with an address (opcode sets count 1-28, reduced size due to above opcodes)
						printf("\tHTEX %d,(>83%02X)\n", x-0xe0+1, rom[offset]);
						offset++;	// address is one byte in the scratchpad RAM
						break;
				}
		}
	}

	printf("Exitting FMT at >%04x\n", offset);

	return true;
}

// parses and skips over a GPL argument - these are non-GROM (so no patching)
void skip_arg(int &offset) {
	if ((rom[offset] & 0x80)==0) {
		// scratchpad direct - one byte
		offset++;
		return;
	}
	if ((rom[offset] & 0xc0) == 0x80) {
		// type 2...?
		if ((rom[offset]&0x0f) == 0x0f) {
			// type 4 extended - 3 bytes
			offset+=3;
		} else {
			// type 2 normal - 2 bytes
			offset+=2;
		}
		return;
	}
	// else it's type 3 or 5...
	if ((rom[offset]&0x0f) == 0x0f) {
		// type 5 extended/indexed - 4 bytes
		offset+=4;
	} else {
		// type 3 indexed - 3 bytes
		offset+=3;
	}
}

bool handle_move(int &offset) {
	// it's MOVE count, dest, source
	int op = rom[offset-1];		// it was already incremented, and we need some control bits
	
	// check count
	if (op & 0x01) {
		// immediate - two bytes
		offset+=2;
	} else {
		// generic - need to parse
		skip_arg(offset);
	}

	// check destination
	if (op & 0x08) {
		// VDP register - 1 bytes
		offset++;
	} else {
		// address of some kind
		if (op & 0x10) {
			// parse it
			skip_arg(offset);
		} else {
			// GROM, so just two bytes - but we may need to patch them
			patchword(offset);
			offset+=2;
		}
	}

	// check source
	if (op & 0x04) {
		// standard address
		skip_arg(offset);
	} else {
		// GROM memory, two bytes
		patchword(offset);
		offset+=2;
	}
	if (op & 0x02) {
		// indexed GROM - one more byte
		offset++;
	}

	return true;
}

bool handle_brbs(int &offset) {
	// br and bs have a 6k address combined with the opcode and 1 byte argument
	// however, we want to remember that address for later testing
	int op = rom[offset-1];
	int ad = ((op&0x1f)<<8) | rom[offset];
	ad+=nSrc;
	AreasToLook[nAreas++] = ad;
	offset++;

	// these zero the condition bit, but we use it to see if we should end this thread
	if ((op&0xe0) == 0x40) {
		// BR!
		cond = cond & (~COND_BR);
		if (cond == 0) return false;	// both paths tested
	} else {
		// BS!
		cond = cond & (~COND_BS);
		if (cond == 0) return false;	// both paths tested
	}

	return true;
}

bool handle_type5(int &offset) {
	// just a destination opcode
	skip_arg(offset);
	return true;
}

bool handle_type1(int &offset) {
	int op = rom[offset-1];

	// destination first
	skip_arg(offset);

	// now the source
	if (op & 0x02) {
		// direct operand
		if (op & 0x01) {
			// word operand
			offset+=2;
		} else {
			// byte
			offset++;
		}
	} else {
		// typical operand
		skip_arg(offset);
	}
	return true;
}

// only takes care of those not handled explicitly above
void updatecond(int opcode) {
	if (
		((opcode>=0xc4)&&(opcode<=0xdb)) ||
		((opcode>=0xec)&&(opcode<=0xef)) ||
		((opcode>=0x8e)&&(opcode<=0x8f)) ||
		((opcode>=0x9)&&(opcode<=0xa)) ||
		((opcode>=0x0c)&&(opcode<=0x0d)) ||
		(opcode==0x03) || //(opcode == 0x06) ||	// omitting CALL
		(opcode == 0x0f) ||						// XML can do anything
		(opcode == 0x8a) || (opcode == 0x8b)	// these two, CASE, are not supposed to set the flags, but otherwise we end processing
		) {
			cond = COND_SET;	// could be anything
	}
}

void parsecode(int offset) {
	// starting at offset, parse code. Assume CALLs and XMLs always return eventually, store branches as new areas to look at
	if (offset > nSrc) offset-=nSrc;

	printf("Processing at >%04X\n", offset+nSrc);
	cond = COND_SET;	// neither flag tested (assumption that cases that mean to do both will be on one path)

	for (;;) {
		if (Tested[offset]) break;		// already done this path
		Tested[offset]=true;
		// what is it?
		int c = rom[offset++];
		if (fctns[c] == endpath) break;	// done with this one
		if (!fctns[c](offset)) break;
		if (offset >= RomSize) break;
		updatecond(c);
	}

	printf("Function complete at >%04X.\n", offset+nSrc);
}

int _tmain(int argc, _TCHAR* argv[])
{
	fp=fopen(argv[1], "rb");
	if (NULL == fp) {
		printf("Can't open files: gromrelocate <infile> <outfile> - GROM header required, assumes >6000 base\n");
		return -1;
	}

	printf("\nBuilding function table..\n");
	for (int i=0; i<256; i++) fctns[i]=endpath;		// default - ends a process path (rtn and the like)
	for (int i=0x03; i<=0x03; i++) fctns[i]=skip0;
	for (int i=0x04; i<=0x04; i++) fctns[i]=skip1;
	for (int i=0x05; i<=0x05; i++) fctns[i]=handle_b;
	for (int i=0x06; i<=0x06; i++) fctns[i]=handle_call;
	for (int i=0x07; i<=0x07; i++) fctns[i]=skip1;
	for (int i=0x08; i<=0x08; i++) fctns[i]=handle_fmt;
	for (int i=0x09; i<=0x0a; i++) fctns[i]=skip0;
	for (int i=0x0c; i<=0x0d; i++) fctns[i]=skip0;
	for (int i=0x0e; i<=0x0f; i++) fctns[i]=skip1;
	for (int i=0x20; i<=0x3f; i++) fctns[i]=handle_move;
	for (int i=0x40; i<=0x7f; i++) fctns[i]=handle_brbs;
	for (int i=0x80; i<=0x97; i++) fctns[i]=handle_type5;
	for (int i=0xa0; i<=0xef; i++) fctns[i]=handle_type1;
	for (int i=0xf4; i<=0xfb; i++) fctns[i]=handle_type1;

	memset(AreasToLook, 0, sizeof(AreasToLook));
	memset(Tested, 0, sizeof(Tested));
	memset(rom, 0, sizeof(rom));

	RomSize = fread(rom, 1, 65536, fp);
	fclose(fp);
	printf("Read %d bytes\n", RomSize);

	if (rom[0] != 0xaa) {
		printf("No GROM header found!\n");
		return -1;
	}

	nAreas=0;
	// start with the header
	if (getword(4) != 0) {
		printf("Found powerup list at >%04x", getword(4));
		patchword(4);
		int x=getword(4);
		while (x != 0) {
			printf("  >%04X", getword(x+2));
			AreasToLook[nAreas++] = getword(x+2);
			patchword(x+2);
			x=getword(x);
		}
	}
	if (getword(6) != 0) {
		printf("Found Program list at >%04x", getword(6));
		int x=getword(6);
		patchword(6);
		while (x != 0) {
			char buf[32];
			memset(buf,0,sizeof(buf));
			memcpy(buf, &rom[x+5-nSrc], rom[x+4-nSrc]);
			printf("  >%04X - %s", getword(x+2), buf);
			AreasToLook[nAreas++] = getword(x+2);
			patchword(x+2);
			x=getword(x);
		}
	}
	if (getword(8) != 0) {
		printf("Found DSR list at >%04x", getword(8));
		int x=getword(8);
		patchword(8);
		while (x != 0) {
			char buf[32];
			memset(buf,0,sizeof(buf));
			memcpy(buf, &rom[x+5-nSrc], rom[x+4-nSrc]);
			printf("  >%04X - %s", getword(x+2), buf);
			AreasToLook[nAreas++] = getword(x+2);
			patchword(x+2);
			x=getword(x);
		}
	}
	if (getword(10) != 0) {
		printf("Found subprogram list at >%04x", getword(10));
		int x=getword(10);
		patchword(10);
		while (x != 0) {
			char buf[32];
			memset(buf,0,sizeof(buf));
			memcpy(buf, &rom[x+5-nSrc], rom[x+4-nSrc]);
			printf("  >%04X - %s", getword(x+2), buf);
			AreasToLook[nAreas++] = getword(x+2);
			patchword(x+2);
			x=getword(x);
		}
	}
	if (getword(12) != 0) {
		printf("Found interrupt list at >%04x", getword(12));
		int x=getword(12);
		patchword(12);
		while (x != 0) {
			printf("  >%04X", getword(x+2));
			AreasToLook[nAreas++] = getword(x+2);
			patchword(x+2);
			x=getword(x);
		}
	}

	int idx=0;
	while (idx < nAreas) {
		parsecode(AreasToLook[idx++]);
	}

	fp=fopen(argv[2], "wb");
	if (NULL == fp) {
		printf("Failed to output file\n");
		return -1;
	}
	fwrite(rom, 1, RomSize, fp);
	fclose(fp);

	printf("\ncomplete!\n");

	return 0;
}

