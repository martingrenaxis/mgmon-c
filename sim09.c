/*
 *
 * 6809 Software emulator, base program.
 *
 *  This program will emulate a 6809 CPU in all its instructions,
 *  including the V (overflow) flag.
 *
 *  Written 890502 by Martin Gren
 *  July 2026: V flag implemented, verified against flexemu's 6809 core.
 *
 */

#include "typedef.h"
#include "mgmon.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Forward declarations */
void implied(void);
void oneaddr(void);
void twoaddr(void);
void adrmode(void);
void branch(void);
void ccadj(void);
void ccset(void);
void push(void);
void pul(void);
void page2(void);
void page3(void);
void divd(void);
void divq(void);
void pseudo_save(void);
void pseudo_load(void);
void do_run(void);
/* From mgmon.c */
void prtchr(char ch);
char getchr(void);
int  check_esc(void);
int  sensekey(void);
void term_set_icrnl(int on);
void prtmsg(char *s);
void regs(void);
int  ignblk(void);
int32 getadr(void);
void prtadr(int32 adr);
/* From dis09.c */
void dis(void);

/* ADM3A parser state for the "prt" pseudo-instruction's console output -
   see adm3a_putc() in mgmon.c and the reset in do_run() below. */
static int adm3a_state;
static int adm3a_row;

/* Addressingmode  is defined as follows:

        0       Not allowed
        1       direct addressing
        2       page 2 instruction
        3       page 3 instruction
        4       implied
        5       immediate addressing mode
        6       indexed addressing mode
        7       extended addressing mode
        8       relative addressing (long offset)
        9       psh/pul instruction
       10       accumulator A instruction (implied)
       11       accumulator B instruction (implied)
       12       relative addressing (short offset)
       13       tfr/exg instruction
*/

byte instadrm[0x100] = {1,0,0,1,1,0,1,1,1,1,1,0,1,1,1,1,
                        2,3,4,4,0,0,8,8,0,4,5,0,5,4,13,13,
                        12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
                        6,6,6,6,9,9,9,9,0,4,4,4,4,4,0,4,
                        10,0,0,10,10,0,10,10,10,10,10,0,10,10,0,10,
                        11,0,0,11,11,0,11,11,11,11,11,0,11,11,0,11,
                        6,0,0,6,6,0,6,6,6,6,6,0,6,6,6,6,
                        7,0,0,7,7,0,7,7,7,7,7,0,7,7,7,7,

                        5,5,5,5,5,5,5,0,5,5,5,5,5,12,5,0,
                        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                        5,5,5,5,5,5,5,0,5,5,5,5,5,0,5,0,
                        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};

/* instruction type is defined as follows:

        0       undefined
        1       1 - address instruction with addressing mode (neg ,x+)
        2       2 - address instruction A type opcode 80-BF
        3       2 - address instruction B type opcode > C0
        4       accumulator A implied instruction
        5       accumulator B implied instruction
        6       Short branch instruction (currently also long branch)
        7*      Long branch instruction (not used)
        8       psh instruction
        9       pul instruction
       10       page 2 instruction
       11       page 3 instruction
       12       lea instruction
       13       lbra instruction
       14       lbsr instruction
       15       daa instruction
       16       orcc instruction
       17       andcc instruction
       18       sex instruction
       19       exg instruction
       20       tfr instruction
       21       rts instruction
       22       abx instruction
       23       rti instruction
       24       cwai instruction
       25       mul instruction
       26       swi  instruction
       27       swi2 instruction
       28       swi3 instruction
       29       nop instruction
       30       sync instruction
       31       bsr instruction
       32       Print accumulator
       33       Get character in A
       34       Stop program and return the control
       35       Delay 1 ms
       36       Sense key (non-blocking): A = key code, or 0 if none waiting
       37       Save: X=filename (ASCII, dot included, terminated by any
                byte < $21), Y=start addr, U=end addr (inclusive) -
                writes mem[Y..U] to that host file
       38       Load: X=filename (same terminator rule), Y=start addr -
                reads the whole host file into mem[] from Y, then leaves
                Y pointing at the last byte written

        All other codes are both undefined and illegal.
*/

byte instruc[0x100] = { 1,32,33,1,1,34,1,1,1,1,1,35,1,1,1,1,
                        10,11,29,30,36,38,13,14,37,15,16,0,17,18,19,20,
                        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                        12,12,12,12,8,9,8,9,0,21,22,23,24,25,0,26,
                        4,0,0,4,4,0,4,4,4,4,4,0,4,4,0,4,
                        5,0,0,5,5,0,5,5,5,5,5,0,5,5,0,5,
                        1,0,0,1,1,0,1,1,1,1,1,0,1,1,1,1,
                        1,0,0,1,1,0,1,1,1,1,1,0,1,1,1,1,

                        2,2,2,2,2,2,2,0,2,2,2,2,2,31,2,0,
                        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                        3,3,3,3,3,3,3,0,3,3,3,3,3,0,3,0,
                        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
                        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
                        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};

/*
 * accumulator:
 *
 *       accu[0] = b;
 *       accu[1] = a;
 *       accu[2] = cc;
 *       accu[3] = dp;
 *
 */


unsigned char accu[5],page,aslask,atyp,opcode,ityp,reg,areg;
unsigned char src,dst,leareg,bryt,autostop;

/* 6309 ACCE/ACCF - only used as the W register (E:F, E high) by DIVD/DIVQ
   below. Nothing else in this simulator implements 6309 native mode
   (MD register, TFM, bit instructions, etc.) - these two exist solely
   because DIVQ's dividend/quotient placement needs them. */
unsigned char acce,accf;

/* index register resides in ix[]
        ix[0] = x;
        ix[1] = y;
        ix[2] = u;
        ix[3] = s;
        ix[4] = pc; (only when prepared, not normally)
        ix[5] = d;  (only when prepared, not normally)
*/

unint16 ix[7],ixslask,ixslask2;


/* 6809 status flags: */

int c,v,n,z;

/* Half-carry (carry out of bit 3): set by 8-bit ADD/ADC, consumed by DAA
   below - not part of the c/v/n/z group that ccadj()/ccset() shuttle to
   and from the CC register, since nothing else in this simulator needs
   H round-tripped through PSH/PUL/TFR/EXG. */
int h;


/* This part of the program will take care of implied accumulator
 *  addressing mode.
 */

void implied(void)
{


    switch(opcode & 0x0F)
    {
/*neg*/ case 0: v = (accu[areg] == 0x80);
                c = (accu[areg] != 0);
                z = ((accu[areg] = -accu[areg]) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*com*/ case 3: z = ((accu[areg] = 0xFF - accu[areg]) == 0);
                n = (accu[areg] >= 0x80);
                c = 1;
                v = 0;
                break;
/*lsr*/ case 4: c = (accu[areg]&1);        /* V is left unaffected by LSR */
                z = ((accu[areg] = accu[areg] / 2) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*ror*/ case 6: aslask = 0;                /* V is left unaffected by ROR */
                if (c != 0)
                  aslask = 0x80;
                c = (accu[areg] & 1);
                z = ((accu[areg] = accu[areg] / 2 + aslask) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*asr*/ case 7: c = (accu[areg] & 1);       /* V is left unaffected by ASR */
                accu[areg] /= 2;
                if (accu[areg] >= 0x40)
                  accu[areg] += 0x80;
                z = (accu[areg] == 0);
                n = (accu[areg] >= 0x80);
                break;
/*asl*/ case 8: v = ((accu[areg] >> 7) ^ (accu[areg] >> 6)) & 1;
                c = (accu[areg] >= 0x80);
                z = ((accu[areg] += accu[areg]) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*rol*/ case 9: aslask = c;
                c = ((accu[areg] >= 0x80) & 1);
                z = ((accu[areg] = accu[areg] + accu[areg] + aslask) == 0);
                n = (accu[areg] >= 0x80);
                v = c ^ n;
                break;
/*dec*/ case 10: v = (accu[areg] == 0x80);
                z = ((--accu[areg]) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*inc*/ case 12: v = (accu[areg] == 0x7F);
                z = ((++accu[areg]) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*tst*/ case 13: z = (accu[areg] == 0);
                n = (accu[areg] >= 0x80);
                v = 0;
                break;
/*clr*/ case 15: accu[areg] = 0;
                z = 1;
                n = 0;
                c = 0;
                v = 0;
                break;
        }
}


/* Here we have the instructions for modifying memory
   using some kind of addressing mode.
*/

void oneaddr(void)
{
     adrmode();
     switch(opcode & 0x0F)
     {

/*neg*/ case 0: v = (mem[addr] == 0x80);
                c = (mem[addr] != 0);
                z = ((mem[addr] = -mem[addr]) == 0);
                n = (mem[addr] >= 0x80);
                break;
/*com*/ case 3: mem[addr] = ~mem[addr];
                z = (mem[addr] == 0);
                n = (mem[addr] >= 0x80);
                c = 1;
                v = 0;
                break;
/*lsr*/ case 4: c = (mem[addr]&1);        /* V is left unaffected by LSR */
                z = (((mem[addr]) = (mem[addr]) / 2) == 0);
                n = (mem[addr] >= 0x80);
                break;
/*ror*/ case 6: aslask = 0;               /* V is left unaffected by ROR */
                if (c != 0)
                  aslask = 0x80;
                c = (mem[addr]&1);
                z = (((mem[addr]) = (mem[addr]) / 2 + aslask) == 0);
                n = (mem[addr] >= 0x80);
                break;
/*asr*/ case 7: c = (mem[addr] & 1);      /* V is left unaffected by ASR */
                (mem[addr]) /= 2;
                if (mem[addr] >= 0x40)
                  (mem[addr]) += 0x80;
                z = (mem[addr] == 0);
                n = (mem[addr] >= 0x80);
                break;
/*asl*/ case 8: v = ((mem[addr] >> 7) ^ (mem[addr] >> 6)) & 1;
                c = (mem[addr] >= 0x80);
                z = (((mem[addr]) *= 2) == 0);
                n = (mem[addr] >= 0x80);
                break;
/*rol*/ case 9: aslask = c;
                c = ((mem[addr] >= 0x80) & 1);
                z = (((mem[addr]) = (mem[addr]) + (mem[addr]) + aslask) == 0);
                n = (mem[addr] >= 0x80);
                v = c ^ n;
                break;
/*dec*/ case 10: v = (mem[addr] == 0x80);
                mem[addr]--;
                z = (mem[addr] == 0);
                n = (mem[addr] >= 0x80);
                break;
/*inc*/ case 12: v = (mem[addr] == 0x7F);
                mem[addr]++;
                z = (mem[addr] == 0);
                n = (mem[addr] >= 0x80);
                break;
/*tst*/ case 13: z = (mem[addr] == 0);
                n = (mem[addr] >= 0x80);
                v = 0;
                break;
/*jmp*/ case 14: pc = addr;
                break;
/*clr*/ case 15: mem[addr] = 0;
                z = 1;
                n = 0;
                v = 0;
                c = 0;
                break;
     }
}

/* Here we have the two address instructions (including load and store).
   They use "multiple addressing modes, as seen.
   This part does not take care of the special case instructions (like jsr)
*/

void twoaddr(void)
{
    adrmode();
    switch(opcode & 0x0F)
    {


/*sub*/ case 0: {
                  int wr = (int)accu[areg] - (int)mem[addr];
                  v = ((accu[areg] ^ mem[addr] ^ wr ^ (wr >> 1)) >> 7) & 1;
                }
                c = (accu[areg] < mem[addr]);
                z = ((accu[areg] = accu[areg]-mem[addr]) == 0);
                n = (accu[areg] >= 0x80);
                break;
/*cmp*/ case 1: {
                  int wr = (int)accu[areg] - (int)mem[addr];
                  v = ((accu[areg] ^ mem[addr] ^ wr ^ (wr >> 1)) >> 7) & 1;
                  aslask = (byte)wr;
                }
                z = (aslask == 0);
                n = (aslask >= 0x80);
                c = (accu[areg] < mem[addr]);
                break;
/*sbc*/ case 2: {
                  int cin = c;
                  int wr = (int)accu[areg] - (int)mem[addr] - cin;
                  v = ((accu[areg] ^ mem[addr] ^ wr ^ (wr >> 1)) >> 7) & 1;
                  c = (accu[areg] < mem[addr] + cin);
                  z = ((accu[areg] = accu[areg]-mem[addr]-cin) == 0);
                }
                n = (accu[areg] >= 0x80);
                break;

/* subd and addd instructions  */

        case 3: if (page == 0)
                    if(areg == 0)
                    {
                      int wr;
                      ix[5] = accu[1]*0x100 + accu[0];
                      ixslask = mem[addr] * 0x100 + mem[addr + 1];
                      wr = (int)ix[5] + (int)ixslask;
                      v = ((ix[5] ^ ixslask ^ wr ^ (wr >> 1)) >> 15) & 1;
                      c = (wr >= 0x10000);
                      ixslask = accu[0] + mem[addr + 1];
                      accu[1] +=  mem[addr];
                      if (ixslask >= 0x100)
                        accu[1]++;
                      accu[0] = ixslask & 0xFF;
                      z = (accu[1] == 0) && (accu[0] == 0 );
                      n = (accu[1] >= 0x80);
                    }
                    else
                    {
                      int wr;
                      ix[5] = accu[1] * 0x100 + accu[0];
                      ixslask = mem[addr] * 0x100 + mem[addr + 1];
                      wr = (int)ix[5] - (int)ixslask;
                      v = ((ix[5] ^ ixslask ^ wr ^ (wr >> 1)) >> 15) & 1;
                      if (ix[5] < ixslask)
                        c = 1;
                      else
                        c = 0;
                      ix[5] -= ixslask;
                      accu[1] = ix[5] / 0x100;
                      accu[0] = ix[5] & 0xFF;
                      z = (accu[1] == 0) && (accu[0] == 0 );
                      n = (accu[1] >= 0x80);
                    }
                else
                {
                    /* CMPD (page==1) compares against D (A:B); CMPU
                       (page==2) compares against U (ix[2]) - neither
                       is an indexed register lookup like CMPX/Y/S. */
                    int wr;
                    ixslask = mem[addr] * 0x100 + mem[addr + 1];
                    if (page == 1)
                       ix[5] = accu[1] * 0x100 + accu[0];
                    else
                       ix[5] = ix[2];
                    wr = (int)ix[5] - (int)ixslask;
                    v = ((ix[5] ^ ixslask ^ wr ^ (wr >> 1)) >> 15) & 1;
                    z = ((ixslask2 = ix[5] - ixslask) == 0);
                    n = (ixslask2 >= 0x8000);
                    c = (ix[5] < ixslask);
                }
                if (instadrm[opcode] == 5)
                  pc++;
                break;

/*and*/ case 4: z = ((accu[areg] = (accu[areg] & mem[addr])) == 0);
                n = (accu[areg] >= 0x80);
                v = 0;
                break;
/*bit*/ case 5: z = ((aslask = (accu[areg] & mem[addr])) == 0);
                n = (aslask >= 0x80);
                v = 0;
                break;
/*ld*/  case 6: z = ((accu[areg] = mem[addr]) == 0);
                n = (accu[areg] >= 0x80);
                v = 0;
                break;
/*st*/  case 7: z = ((mem[addr] = accu[areg]) == 0);
                n = (accu[areg] >= 0x80);
                v = 0;
                break;
/*eor*/ case 8: z = ((accu[areg] = (accu[areg] ^ mem[addr])) == 0);
                n = (accu[areg] >= 0x80);
                v = 0;
                break;
/*adc*/ case 9: {
                  int cin = c;
                  int wr = (int)accu[areg] + (int)mem[addr] + cin;
                  v = ((accu[areg] ^ mem[addr] ^ wr ^ (wr >> 1)) >> 7) & 1;
                  h = ((accu[areg] ^ mem[addr] ^ wr) & 0x10) != 0;
                  aslask = accu[areg];
                  z = ((accu[areg] = accu[areg] + mem[addr] + cin) == 0);
                }
                n = (accu[areg] >= 0x80);
                if (accu[areg] < aslask)
                  c = 1;
                else
                  c = 0;
                break;
/*or*/  case 10: z = ((accu[areg] = (accu[areg] | mem[addr])) == 0);
                n = (accu[areg] >= 0x80);
                v = 0;
                break;
/*add*/ case 11: {
                  int wr = (int)accu[areg] + (int)mem[addr];
                  v = ((accu[areg] ^ mem[addr] ^ wr ^ (wr >> 1)) >> 7) & 1;
                  h = ((accu[areg] ^ mem[addr] ^ wr) & 0x10) != 0;
                }
                aslask = accu[areg];
                z = ((accu[areg] = (accu[areg] + mem[addr])) == 0);
                n = (accu[areg] >= 0x80);
                if (accu[areg] < aslask)
                  c = 1;
                else
                  c = 0;
                break;

/* cmpx or ldd */

        case 12: if (areg != 0)
                {
                  int wr;
                  areg = page;
                  if (areg == 2)
                     areg = 3;
                  ixslask = mem[addr] * 0x100 + mem[addr + 1];
                  wr = (int)ix[areg] - (int)ixslask;
                  v = ((ix[areg] ^ ixslask ^ wr ^ (wr >> 1)) >> 15) & 1;
                  z = ((ixslask2=ix[areg]-ixslask) == 0);
                  n = (ixslask2 >= 0x8000);
                  c = (ix[areg] < ixslask);
                }
                else
                {
                  accu[1] = mem[addr];
                  accu[0] = mem[addr + 1];
                  n = (accu[1] >= 0x80);
                  z = ((accu[0] == 0 ) && (accu[1] == 0));
                  v = 0;
                }
                if (instadrm[opcode] == 5)
                  pc++;
                break;

/* Jump to subroutine and store D register in memory */

        case 13: if (areg == 0)
                {
                  z = (accu[1] == 0) && (accu[0] == 0 );
                  n = (accu[1] >= 0x80);
                  v = 0;
                  mem[addr] = accu[1];
                  mem[addr + 1] = accu[0];
                }
                else
                {
                  mem[--ix[3]] = pc & 0xFF;
                  mem[--ix[3]] = pc / 0x100;
                  pc = addr;
                }
		break;

/* ldx and ldu instructions */

        case 14: areg = 2-(areg*2) + page;
                z = ((ix[areg] = mem[addr] * 0x100 + mem[addr + 1]) == 0);
                n = (ix[areg] >= 0x8000);
                v = 0;
                if (instadrm[opcode] == 5)
                   pc++;
                break;

/* stx and stu instrucitons */

        case 15: areg = 2 - areg * 2 + page;
                z = (ix[areg] == 0);
                n = (ix[areg] >= 0x8000);
                v = 0;
                mem[addr] = ix[areg] / 0x100;
                mem[addr + 1] = ix[areg] & 0xFF;
                break;
    }
}



void adrmode(void)
{
    switch(instadrm[opcode])
    {
/*dir*/ case 1: addr = mem[pc++] + accu[3] * 0x100;
                break;
/*ext*/ case 7: addr = mem[pc] * 0x100 + mem[pc + 1];
                pc += 2;
                break;
/*imm*/ case 5: addr = pc++;
                break;
/*ix*/  case 6: ityp = mem[pc++];
                reg = (ityp / 0x20) & 3;
                if (ityp < 0x80)
                {
                  if ((ityp & 0x10) != 0x10)
                    addr = ix[reg] + (ityp & 0x0F);
                  else
                    addr = ix[reg] - 0x10 + (ityp & 0x0F);
                }
                else
                {
                  switch(ityp & 0x0F)
                  {
                    case 0:  addr = ix[reg]++;
                             break;
                    case 1:  addr = ix[reg]++;
                             ix[reg]++;
                             break;
                    case 2:  addr = --ix[reg];
                             break;
                    case 3:  ix[reg]--;
                             addr = (--ix[reg]);
                             break;
                    case 4:  addr = ix[reg];
                             break;
                    case 5:  if (accu[0] >= 0x80)
                               addr = (ix[reg])-0x100 + accu[0];
                             else
                               addr = (ix[reg]) + accu[0];
                             break;
                    case 6:  if (accu[1] >= 0x80)
                               addr = ix[reg]-0x100 + accu[1];
                             else
                               addr = ix[reg] + accu[1];
                             break;
                    case 8:  addr = (ix[reg]) + (aslask = mem[pc++]);
                             if (aslask >= 0x80)
                               addr -= 0x100;
                             break;
                    case 9:  addr = ix[reg] + mem[pc] * 0x100 + mem[pc + 1];
                             pc += 2;
                             break;
                    case 11: addr = ix[reg] + accu[1] * 0x100 + accu[0];
                             break;
                    case 12: aslask = mem[pc++];
                             addr = pc + aslask;
                             if (aslask >= 0x80)
                               addr -= 0x100;
                             break;
		    case 13: addr = pc + mem[pc] * 0x100 + mem[pc + 1] + 2;
                             pc += 2;
                             break;
                    case 15: addr = mem[pc] * 0x100 + mem[pc + 1];
                             pc += 2;
                             break;
                  }
                  if((ityp & 0x10) == 0x10)
                  {
                    ixslask = mem[addr] * 0x100 + mem[addr + 1];
                    addr = ixslask;
                  }
                }
    }      
}

void branch(void)
{
    if (opcode <  0x20)
    {
        ixslask = mem[pc]*0x100 + mem[pc + 1];
        pc += 2;
    }
    else
    {
        ixslask = mem[pc++];
        if(ixslask >= 0x80)
           ixslask -= 0x100;
    }
    switch(opcode & 0x0F)
    {
        case 0: pc += ixslask;
                break;
        case 1: break;
        case 2: if ((z == 0) && (c == 0))
                   pc += ixslask;
                break;
        case 3: if ((z == 1) || (c == 1))
                   pc += ixslask;
                break;
        case 4: if (c == 0)
                   pc += ixslask;
                break;
        case 5: if (c == 1)
                   pc += ixslask;
                break;
        case 6: if (z == 0)
                   pc += ixslask;
                break;
        case 7: if (z == 1)
                   pc += ixslask;
                break;
        case 8: if (v == 0)                 /* BVC */
                   pc += ixslask;
                break;
        case 9: if (v == 1)                 /* BVS */
                   pc += ixslask;
                break;
        case 10: if (n == 0)                /* BPL */
                   pc += ixslask;
                 break;
        case 11: if (n == 1)                /* BMI */
                   pc += ixslask;
                 break;
        case 12: if ((n ^ v) == 0)          /* BGE */
                   pc += ixslask;
                 break;
        case 13: if ((n ^ v) == 1)          /* BLT */
                   pc += ixslask;
                 break;
        case 14: if ((z == 0) && ((n ^ v) == 0))   /* BGT */
                   pc += ixslask;
                 break;
        case 15: if ((z == 1) || ((n ^ v) == 1))   /* BLE */
                   pc += ixslask;
                 break;
    }
}

void ccadj(void)
{
        accu[2] = (accu[2] & 0xF0) + (c & 1) + (v & 1)*2 + (z & 1)*4 + (n & 1)*8;
}

void ccset(void)
{
        c = accu[2] & 1;
        v = (accu[2]/2 ) &1;
        z = (accu[2]/4 ) &1;
        n = (accu[2]/8 ) &1;
}

void push(void)
{
        int stk;
        stk = 3 - (opcode &2)/2;     /* 3 if s-stack, 2 if u stack */
        if (aslask  >= 0x80)
        {
          mem[--ix[stk]] = pc & 0xFF;
          mem[--ix[stk]] = pc / 0x100;
        }
        if ((aslask & 0x40) != 0)
        {
          mem[--ix[stk]] = ix[stk^1] & 0xFF;
          mem[--ix[stk]] = ix[stk^1] / 0x100;
        }
        if ((aslask & 0x20) != 0)
        {
          mem[--ix[stk]] = ix[1] & 0xFF;
          mem[--ix[stk]] = ix[1] / 0x100;
        }
        if ((aslask & 0x10) != 0)
        {
          mem[--ix[stk]] = ix[0] & 0xFF;
          mem[--ix[stk]] = ix[0] / 0x100;
        }
        if ((aslask & 0x08) != 0)
          mem[--ix[stk]] = accu[3];
        if ((aslask & 04) != 0)
          mem[--ix[stk]] = accu[0];
        if ((aslask & 02) != 0)
          mem[--ix[stk]] = accu[1];
        if ((aslask & 01) != 0)
        {
          ccadj();
          mem[--ix[stk]] = accu[2];
        }
}

void pul(void)
{
        int stk;
        stk = 3 - (opcode &2)/2;     /* 3 if s-stack, 2 if u stack */

        if ((aslask & 01) != 0)
        {
          accu[2] = mem[ix[stk]++];
          ccset();
        }
        if ((aslask & 02) != 0)
          accu[1] = mem[ix[stk]++];
        if ((aslask & 04) != 0)
          accu[0] = mem[ix[stk]++];
        if ((aslask & 0x08) != 0)
          accu[3] = mem[ix[stk]++];

        if ((aslask & 0x10) != 0)
        {
          ix[0] = mem[ix[stk]++] * 0x100;
          ix[0] += mem[ix[stk]++];
        }
        if ((aslask & 0x20) != 0)
        {
          ix[1] = mem[ix[stk]++] * 0x100;
          ix[1] += mem[ix[stk]++];
        }
        if ((aslask & 0x40) != 0)
        {
          ix[stk^1] = mem[ix[stk]++] * 0x100;
          ix[stk^1] += mem[ix[stk]++];
        }
        if ((aslask & 0x80) != 0)
        {
          pc = mem[ix[stk]++] * 0x100;
          pc += mem[ix[stk]++];
        }
}

/* "save"/"load" pseudo-instructions (opcodes $18/$15 - not real 6809/6309
   opcodes, repurposed illegal slots, same idea as prt/get/dly/sensekey
   above). X points to an ASCII filename, dot included (e.g. "TEST.BIN"),
   terminated by any byte < $21 - used as-is (whatever case it's stored
   in) as a host filename, opened relative to mgmon's working directory. */

#define PSEUDO_FNAME_MAX 32

static int pseudo_getfname(char *buf)
{
    int i;
    unint16 a = ix[0];

    for (i = 0; i < PSEUDO_FNAME_MAX - 1 && mem[a] >= 0x21; i++, a++)
        buf[i] = (char)mem[a];
    buf[i] = '\0';
    return i;
}

/* save: Y=start addr, U=end addr (inclusive) - writes mem[Y..U] to the
   host file. Y>U (nothing to save) writes an empty file. */

void pseudo_save(void)
{
    char fname[PSEUDO_FNAME_MAX];
    FILE *f;
    unint32 len, i;

    if (pseudo_getfname(fname) == 0)
    {
        prtmsg("save: empty filename\n");
        return;
    }
    f = fopen(fname, "wb");
    if (f == NULL)
    {
        prtmsg("save: can't create ");
        prtmsg(fname);
        prtmsg("\n");
        return;
    }
    len = (ix[2] >= ix[1]) ? ((unint32)ix[2] - ix[1] + 1) : 0;
    for (i = 0; i < len; i++)
        fputc(mem[(unint16)(ix[1] + i)], f);
    fclose(f);
}

/* load: Y=start addr - reads the whole host file into mem[] from Y, then
   leaves Y pointing at the last byte written (unchanged if the file was
   empty - there is no "last byte" to point at). Addresses wrap mod
   0x10000, matching the 6809's 16-bit address space. */

void pseudo_load(void)
{
    char fname[PSEUDO_FNAME_MAX];
    FILE *f;
    int ch, got;
    unint16 a;

    if (pseudo_getfname(fname) == 0)
    {
        prtmsg("load: empty filename\n");
        return;
    }
    f = fopen(fname, "rb");
    if (f == NULL)
    {
        prtmsg("load: can't open ");
        prtmsg(fname);
        prtmsg("\n");
        return;
    }
    a = ix[1];
    got = 0;
    while ((ch = fgetc(f)) != EOF)
    {
        mem[a] = (byte)ch;
        got = 1;
        a++;
    }
    fclose(f);
    if (got)
        ix[1] = a - 1;
}

void page2(void)
{
    opcode = mem[pc++];
    if (opcode < 0x30)
    {
        opcode -= 0x20;
        branch();
    }
    else
    {
        page = 1;
        areg = (opcode < 0xC0);
        twoaddr();
        page = 0;
    }
}

/* 6309 DIVD/DIVQ - ported from MAME's hd6309 core (src/devices/cpu/m6809/
   hd6309.cpp), whose condition-code/overflow handling was reverse
   engineered against real silicon (credited there to Xroar, hoglet67 and
   fuzzing) - Hitachi's own "HD63B09EP Technical Reference Guide" gets the
   DIVD register placement wrong (it describes DIVQ's D/W split for both).
   Confirmed against that source:
     DIVD: D (16-bit, signed) / 8-bit operand  -> quotient in B, remainder in A
     DIVQ: Q=D:W (32-bit, signed) / 16-bit operand -> quotient in W, remainder in D
   The "soft overflow" check below compares the sign of the pre-truncation
   quotient (which can still have high bits set here - the sign-fixup
   above works in a wider domain, e.g. 16 bits for an 8-bit DIVD result)
   against the sign of the truncated register value. They can disagree
   because the negation just above silently declines to negate a quotient
   whose magnitude already sits at the truncated type's negative boundary
   (exactly 128 for DIVD, exactly 32768 for DIVQ) - confirmed against
   DIVD -1024/8 (quotient magnitude 128): B comes back $80 (which reads
   as -128, the mathematically correct value) but with N and V both set,
   where a "normal" negative result would only set N. */

void divd(void)
{
    int dividend, divisor, tmp1, tmp2, quotient, remainder, new_quotient;
    int nsign, vsign;

    divisor = mem[addr];
    if (divisor == 0)
    {
        prtmsg("Division by zero\n");
        return;
    }

    dividend = accu[1] * 0x100 + accu[0];

    nsign = (dividend & 0x8000) != 0;
    tmp1 = nsign ? ((-dividend) & 0xFFFF) : dividend;
    if (nsign)
    {
        /* real hardware overwrites D with the absolute value even when
           what follows turns out to be a range overflow below */
        accu[1] = (tmp1 >> 8) & 0xFF;
        accu[0] = tmp1 & 0xFF;
    }

    vsign = (divisor & 0x80) != 0;
    tmp2 = vsign ? ((-divisor) & 0xFF) : divisor;

    quotient  = tmp1 / tmp2;
    remainder = tmp1 % tmp2;

    c = v = n = z = 0;

    if ((quotient >> 8) != 0)
    {
        v = 1;
        if (nsign)
            n = 1;
        return;
    }

    if (nsign)
        remainder = (-remainder) & 0xFF;

    if (nsign != vsign)
    {
        new_quotient = (-quotient) & 0xFFFF;
        if ((new_quotient & 0x80) && !(quotient & 0x80))
            quotient = new_quotient;
    }

    accu[1] = remainder & 0xFF;
    accu[0] = quotient & 0xFF;

    if (accu[0] & 1)
        c = 1;

    if ((((quotient >> 15) ^ (accu[0] >> 7)) & 1) == 0)
    {
        if (accu[0] == 0)
            z = 1;
        if (accu[0] & 0x80)
            n = 1;
    }
    else
    {
        n = 1;
        v = 1;
    }
}

void divq(void)
{
    unint32 dividend, tmp1, quotient, new_quotient;
    unint16 divisor, tmp2, remainder, wreg;
    int nsign, vsign;

    divisor = mem[addr] * 0x100 + mem[addr + 1];
    if (divisor == 0)
    {
        prtmsg("Division by zero\n");
        return;
    }

    wreg = acce * 0x100 + accf;
    dividend = ((unint32)(unint16)(accu[1] * 0x100 + accu[0]) << 16) | wreg;

    nsign = (dividend & 0x80000000u) != 0;
    tmp1 = nsign ? (0u - dividend) : dividend;
    if (nsign)
    {
        accu[1] = (tmp1 >> 24) & 0xFF;
        accu[0] = (tmp1 >> 16) & 0xFF;
        acce    = (tmp1 >> 8) & 0xFF;
        accf    = tmp1 & 0xFF;
    }

    vsign = (divisor & 0x8000) != 0;
    tmp2 = vsign ? (unint16)((0u - divisor) & 0xFFFF) : divisor;

    quotient  = tmp1 / tmp2;
    remainder = (unint16)(tmp1 % tmp2);

    c = v = n = z = 0;

    if ((quotient >> 16) != 0)
    {
        v = 1;
        if (nsign)
            n = 1;
        return;
    }

    if (nsign)
        remainder = (unint16)((0u - (unint32)remainder) & 0xFFFF);

    if (nsign != vsign)
    {
        new_quotient = (0u - quotient) & 0xFFFF;
        if ((new_quotient & 0x8000) && !(quotient & 0x8000))
            quotient = new_quotient;
    }

    accu[1] = (remainder >> 8) & 0xFF;
    accu[0] = remainder & 0xFF;
    acce    = (quotient >> 8) & 0xFF;
    accf    = quotient & 0xFF;

    if (accf & 1)
        c = 1;

    if ((((quotient >> 31) ^ (acce >> 7)) & 1) == 0)
    {
        if (acce == 0 && accf == 0)
            z = 1;
        if (acce & 0x80)
            n = 1;
    }
    else
    {
        n = 1;
        v = 1;
    }
}

void page3(void)
{
    opcode = mem[pc++];

    /* 6309 DIVD/DIVQ - opcode nibble D/E in the 0x80-0xBF range only;
       0xC0+ page-3 bytes are illegal on real hardware and unaffected. */
    if (opcode < 0xC0 && ((opcode & 0x0F) == 0x0D || (opcode & 0x0F) == 0x0E))
    {
        if (opcode == 0x8D)
            addr = pc++;   /* DIVD immediate: instadrm[0x8D] wrongly says
                               relative-short - that byte means BSR on page 0 */
        else
        {
            adrmode();
            if (opcode == 0x8E)
                pc++;      /* DIVQ immediate: 2nd byte of the 16-bit divisor */
        }
        if ((opcode & 0x0F) == 0x0D)
            divd();
        else
            divq();
        return;
    }

    page = 2;
    areg = (opcode < 0xC0);
    twoaddr();
    page = 0;

}

void do_run(void)
{
  long escchk = 0;

  /* "run" with no argument keeps behaving exactly as before (runs from
     whatever pc already is); "run <addr>" sets pc first and announces
     it, e.g. for jumping straight to a program's entry point. */
  if (ignblk() != CR)
  {
      pc = getadr();
      prtmsg("Running from ");
      prtadr(pc);
      prtmsg("\n");
  }

  /* Fresh ADM3A parser state each time run/step is entered, same as
     do_term() resets on each connection - see adm3a_putc() and the
     "prt" pseudo-instruction (case 32) below. */
  adm3a_state = ADM3A_NORMAL;
  adm3a_row   = 0;

  /* See term_set_icrnl() in mgmon.c: a guest program (FLEX, etc.)
     needs to see a real CR ($0D) from "get" (opcode 2/case 33), not
     the LF mgmon's own parser expects - restored before returning
     either way, single-step break, ESC, or the program's own stop. */
  term_set_icrnl(0);

  while(bryt != 39)
  {
    if (mode_s != 0)
    {
      char kc;
      if (mode_r)
        regs();
      if (mode_i)
      {
          prtmsg("   $");
          dispc = pc;
          dis();
          /* dis() prints through stdio's buffered printf(), while
             prtmsg()/prtchr() write(2) straight to the fd - without this
             flush the trailing "\n" below can reach the terminal before
             the still-buffered instruction text, garbling the line. */
          fflush(stdout);
          prtmsg("\n");
      }
      else
        if (mode_r)
            prtmsg("\n");

      kc = getchr();
      if (kc == CR)
        bryt = 39;
      else if (kc == 0x1B)
      {
        bryt = 39;
        prtmsg("Break (ESC)\n");
      }
    }
    /* Not single-stepping: nothing else in the loop waits on the
       keyboard, so poll for a pending ESC every so often to let the
       user break out of an otherwise uninterruptible run. */
    else if (((++escchk) & 0x7F) == 0 && check_esc())
    {
      bryt = 39;
      prtmsg("Break (ESC)\n");
    }
    if (bryt != 39)
    switch(instruc[opcode = mem[pc++]])
    {
        case 0: prtmsg("Undefined instruction\n");
                break;
        case 1: oneaddr();
                break;
        case 2: areg = 1;
                twoaddr();
                break;
        case 3: areg = 0;
                twoaddr();
                break;
        case 4: areg = 1;
                implied();
                break;
        case 5: areg = 0;
                implied();
                break;
        case 6: branch();
                break;
        case 7: branch();  /* treated by short branch routine */
                break;
        case 8: aslask = mem[pc++];
                push();
                break;
        case 9: aslask = mem[pc++];
                pul();
                break;
        case 10: page2();
                break;
        case 11: page3();
                break;
        case 12: leareg = opcode & 3;
                if (leareg == 2)
                  leareg = 3;
                else
                  if (leareg == 3)
                     leareg = 2;
                adrmode();
                ix[leareg] = addr;
                 if (leareg < 2)
                 {
                    z = (ix[leareg] == 0);
    n = ((ix[leareg] & 0x8000) != 0);
                 }
                 break;
        case 13: ixslask = mem[pc]*0x100 + mem[pc + 1];
                 pc += ixslask + 2;
                 break;
        case 14: ixslask = mem[pc]*0x100 + mem[pc + 1];
                 pc += 2;
                 mem[--ix[3]] = pc & 0xFF;
                 mem[--ix[3]] = pc / 0x100;
                 pc += ixslask;
                 break;
        /* daa - decimal addition adjust, A register only (per MC6809
           datasheet). msn/lsn drive the correction from the two BCD
           nibbles; h (set by the preceding ADDA/ADCA) catches the case
           where a nibble carry occurred without pushing the low nibble
           above 9 (e.g. 0x19 + 0x19 = 0x32, lsn=2). C is only ever set
           by DAA, never cleared, per the datasheet ("set if a carry is
           generated or if the carry bit was set before the operation").
        */
        case 15: {
                 int msn = accu[1] & 0xF0;
                 int lsn = accu[1] & 0x0F;
                 int cf = 0;
                 int t;
                 if (lsn > 9 || h)
                    cf |= 0x06;
                 if (msn > 0x80 && lsn > 9)
                    cf |= 0x60;
                 if (msn > 0x90 || c)
                    cf |= 0x60;
                 t = accu[1] + cf;
                 accu[1] = t & 0xFF;
                 c = c || (t > 0xFF);
                 z = (accu[1] == 0);
                 n = (accu[1] >= 0x80);
                 v = 0;
                 }
                 break;
        case 16: ccadj();
                 accu[2] = accu[2] | mem[pc++];
                 ccset();
                 break;
        case 17: ccadj();
                 accu[2] = accu[2] & mem[pc++];
                 ccset();
                 break;
        case 18: accu[1] = 0;
                 if (accu[0] >= 0x80)
                    accu[1] = 0xFF;
                 break;
        case 19: dst = src = mem[pc++];
                 src /= 0x10;
                 dst = dst & 0x0F;
                 if (dst >= 8)
                 {
                    src = src & 3;
                    dst = dst & 3;
                    if (src <2)
                        src = 1 - src;
                    if (dst <2)
                        dst = 1 - dst;
                    if (src == 2)
                       ccadj();
                    aslask = accu[dst];
                     accu[dst] = accu[src];
                    accu[src] = aslask;
                    if (dst == 2 || src == 2)
                       ccset();
                 }
                 else
                 {
                    if (src == 0)
                        src = 6;
                    if (dst == 0)
                        dst = 6;
                    src--;
                    dst--;
                    ix[4] = pc;
                    ix[5] = accu[1] * 0x100 + accu[0];
                    ixslask = ix[src];
                    ix[src] = ix[dst];
                    ix[dst] = ixslask;
                    if (dst == 5)
                    {
                         accu[1] = ix[5] / 0x100;
                         accu[0] = ix[5] & 0xFF;
                    }
                    if (dst == 4)
                        pc = ix[4];
                 }
                 break;
 /*tfr*/ case 20: dst =  mem[pc++];
                 src = (dst / 0x10) & 0xFF;
                 dst = dst & 0x0F;
                 if (dst >= 8)
                 {
                    src = src & 3;
                    dst = dst & 3;
                    if (src <2)
                        src = 1 - src;
                    if (dst <2)
                        dst = 1 - dst;
                    if (src == 2)
                       ccadj();
                    accu[dst] = accu[src];
                    if (dst == 2)
                       ccset();
                 }
                 else
                 {
                    if (src == 0)
                        src = 6;
                    if (dst == 0)
                        dst = 6;
                    src--;
                    dst--;
                    ix[4] = pc;
                    ix[5] = accu[1]*0x100 + accu[0];
                    ix[dst] = ix[src];
                    if (dst == 5)
                    {
                         accu[1] = ix[5] / 0x100;
                         accu[0] = ix[5] & 0xFF;
                    }
                    if (dst == 4)
                        pc = ix[4];
                 }
                 break;
/*rts*/ case 21: pc = mem[ix[3]++]*0x100;
                 pc += mem[ix[3]++];
                 break;
/*abx*/ case 22: ixslask = accu[0];       /* make sure of unsigned operation */
                 ix[0] += ixslask;
                 break;
/*rti*/ case 23:

/*cwa*/ case 24: prtmsg("not defined\n");
                 break;
/*mul*/ case 25: ixslask = accu[0] * accu[1];
                 accu[0] = ixslask & 0xFF;
                 accu[1] = ixslask/0x100;
                 break;
/*swi*/ case 26:

/*sw2*/ case 27:

/*sw3*/ case 28: prtmsg("SWI:s are not defined\n");
                 break;

/*nop*/ case 29: break;

/*syn*/ case 30: prtmsg("Sync not defined\n");
                 break;
/*bsr*/ case 31: pc++;
                 mem[--ix[3]] = pc & 0xFF;
                 mem[--ix[3]] = pc / 0x100;
                 aslask =  mem[pc-1];
                 pc += aslask;
                 if (aslask >= 0x80)
                 {
                    pc -= 0x100;
                 }
                 break;
/*prt*/ case 32: adm3a_putc(&adm3a_state, &adm3a_row, (unsigned char)accu[1]);
                 break;
/*get*/ case 33: accu[1] = getchr();
                 break;
        case 34: bryt = 39; /* break the program and stop running */
                 break;
/*dly*/ case 35: usleep(1000);     /* delay 1 ms, e.g. to pace serial output */
                 break;
/*sns*/ case 36: accu[1] = sensekey(); /* non-blocking: key code, or 0 if none waiting */
                 break;
/*sav*/ case 37: pseudo_save();
                 break;
/*lod*/ case 38: pseudo_load();
                 break;
    }
    if (mode_p)
       if (pc == break_pc)
           bryt = 39;
    if (mode_q)
       if (pc == break_qpc)
           bryt = 39;
    if (mode_g)
        if (break_g-- == 0)
           bryt = 39;
  }
  bryt = 0;

  term_set_icrnl(1);
}




