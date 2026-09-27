/*
 *
 * 6809 disassembler
 *
 *  This program will dissasemble a 6809 CPU line by line.
 *
 *  Written 890511 by Martin Gren
 */

#include "typedef.h"
#include "mgmon.h"
#include <stdio.h>

/* Forward declarations */
void disonemnem(void);
void disimpl(void);
void disonead(void);
void disacc(void);
void distwoad(void);
void disix(byte reg);
void disadrmode(void);
void disbranch(void);
void dispshpul(void);
void distfr(void);
void disreg(int r);
void dispage2(void);
void dispage3(void);
unint16 dis(void);
/* From mgmon.c */
char getchr(void);
void prtmsg(char *s);
unint16 getadr(void);

unint16 dispc;
int   disaddr;


/*
 *
 * This will dissassemble the one-address and implied instructions
 *  
 */

void disonemnem(void)
{


    switch(opcode & 0x0F)
    {
/*neg*/ case 0: printf("neg");
                break;
/*com*/ case 3: printf("com");
                break;
/*lsr*/ case 4: printf("lsr");
                break;
/*ror*/ case 6: printf("ror");
                break;
/*asr*/ case 7: printf("asr");
                break;
/*asl*/ case 8: printf("asl");
                break;
/*rol*/ case 9: printf("rol");
                break;
/*dec*/ case 10: printf("dec");
                break;
/*inc*/ case 12: printf("inc");
                break;
/*tst*/ case 13: printf("tst");
                break;
/*tst*/ case 14: printf("jmp");
                break;
/*clr*/ case 15: printf("clr");
                break;
        default: printf("undef");
                break;
        }
}

void disimpl(void)
{
    disonemnem();
    if (areg == 0)
        printf("b");
    else
        printf("a");
}

void disonead(void)
{
    disonemnem();
    disadrmode();
}               
        
void disacc(void)
{
    if (areg == 0)
        printf("b");
    else
        printf("a");
}


/*
 *
 * Here we have the two address instructions (including load and store).
 * They use "multiple addressing modes, as seen.
 * This part does not take care of the special case instructions (like jsr)
 *
 */

void distwoad(void)
{
    switch(opcode & 0x0F)
    {
/*sub*/ case 0: printf("sub");
                disacc();
                break;
/*cmp*/ case 1: printf("cmp");
                disacc();
                break;
/*sbc*/ case 2: printf("sbc");
                disacc();
                break;

/* subd and addd instructions  */

        case 3: if (page == 0)
                    if(areg == 0)
                        printf("addd");
                    else
                        printf("subd");
                else
                    if (page == 1)
                        printf("cmpu");
                    else
                        printf("cmpd");
                break;
/*and*/ case 4: printf("and");
                disacc();
                break;
/*bit*/ case 5: printf("bit");
                disacc();
                break;
/*ld*/  case 6: printf("ld");
                disacc();
                break;
/*st*/  case 7: printf("st");
                disacc();
                break;
/*eor*/ case 8: printf("eor");
                disacc();
                break;
/*adc*/ case 9: printf("adc");
                disacc();
                break;
/*or*/  case 10: printf("or");
                disacc();
                break;
/*add*/ case 11: printf("add");
                disacc();
                break;

/* cmpx or ldd */

        case 12: if (areg != 0)
                    switch(page)
                    {
                        case 0: printf("cmpx");
                                break;
                        case 1: printf("cmpy");
                                break;
                        case 2: printf("cmps");
                                break;
                    }
                else
                    printf("ldd");   
                break;

/* Jump to subroutine and store D register in memory */

        case 13: if (areg == 0)
                    printf("std");
                else
                    printf("jsr");
                break;

/* ldx and ldu instructions */

        case 14: printf("ld");
                disix(2-(areg*2)+page);
                break;

/* stx and stu instrucitons */

        case 15: printf("st");
                disix(2-areg*2+page);
                break;
    }
    disadrmode();
}

void disix(byte reg)
{
    switch(reg)
    {
        case 0: printf("x");
                break;
        case 1: printf("y");
                break;
        case 2: printf("u");
                break;
        default:printf("s");
                break;
    }
}
void disadrmode(void)
{
    int reg;
    printf("   ");
    switch(instadrm[opcode])
    {
/*dir*/ case 1: printf("<$%02X",mem[dispc++]);
                break;
/*ext*/ case 7: printf("$%02X%02X",mem[dispc],mem[dispc+1]);
                dispc += 2;
                break;
/*imm*/ case 5: printf("#$%02X",mem[dispc++]);
                if (((opcode & 0x0F) >= 12) || ((opcode & 0x0F) == 3))
                    printf("%02X",mem[dispc++]);
                break;
/*ix*/  case 6: ityp = mem[dispc++];
                if ((ityp & 0x90) == 0x90)
                    printf("[");
                reg = (ityp/0x20) & 3;
                if (ityp < 0x80)
                {
                  if ((ityp & 0x10) == 0)
                    printf("%d,",ityp & 0x0F);
                  else
                    printf("-%d,",16-(ityp & 0x0F));
                  disix(reg);
                }
                else
                {
                  switch(ityp & 0x0F)
                  {
                    case 0:  printf(",");
                             disix(reg);
                             printf("+");
                             break;
                    case 1:  printf(",");
                             disix(reg);
                             printf("++");
                             break;
                    case 2:  printf(",-");
                             disix(reg);
                             break;
                    case 3:  printf(",--");
                             disix(reg);
                             break;
                    case 4:  printf(",");
                             disix(reg);
                             break;
                    case 5:  printf("b,");
                             disix(reg);
                             break;
                    case 6:  printf("a,");
                             disix(reg);
                             break;
                    case 8:  if (mem[dispc] >= 0x80)
				printf("-%d,",256 - mem[dispc++]);
                             else
                                printf("%d,",mem[dispc++]);
                             disix(reg);
                             break;
		    case 9:  disaddr = mem[dispc] * 0x100 + mem[dispc + 1];
                             dispc += 2;
                             if (mem[dispc] >= 0x80)
				printf("-%d,",0x8000 - (disaddr & 0x7FFF));
                             else
                                printf("%d,",disaddr);
                             disix(reg);
                             break;
                    case 11: printf("d,");
                             disix(reg);
                             break;
                    case 12: disaddr = (dispc + 1 + (int)(signed char)mem[dispc]) & 0xFFFF;
                             dispc++;
                             printf("%04X,pcr",disaddr);
                             break;
                    case 13: disaddr = (dispc + mem[dispc]*0x100 + mem[dispc + 1] + 2) & 0xFFFF;
                             printf("%04X,pcr",disaddr);
                             dispc += 2;
                             break;
                    case 15: printf("$%02X%02X",mem[dispc],mem[dispc+1]);
                             dispc += 2;
                             break;
                  }
                  if((ityp & 0x90) == 0x90)
                         printf("]");
                }
    }      
}

void disbranch(void)
{
    if (opcode <  0x20)
    {
        printf("l");
	disaddr = mem[dispc] * 0x100 + mem[dispc + 1] + dispc + 2;
        dispc += 2;
    }
    else
    {
        disaddr = (mem[dispc] + dispc + 1) & 0xFFFF;
        if(mem[dispc] >= 0x80)
           disaddr -= 0x100;
        dispc++;
    }
    switch(opcode & 0x0F)
    {
        case 0: printf("bra");
                break;
        case 1: printf("brn");
                break;
        case 2: printf("bhi");
                break;
        case 3: printf("bls");
                break;
        case 4: printf("bhs");
                break;
        case 5: printf("blo");
                break;
        case 6: printf("bne");
                break;
        case 7: printf("beq");
                break;
        case 8: printf("bvc");
                break;
        case 9: printf("bvs");
                break;
        case 10:printf("bpl");
                 break;
        case 11:printf("bmi");
                 break;
        case 12:printf("bge");
                break;
        case 13:printf("blt");
                break;
        case 14:printf("bgt");
                break;
        case 15:printf("ble");
                break;
    }
    printf("   $%04X",disaddr);
}

void dispshpul(void)
{
int pb;
    if ((opcode & 2) == 0)
        printf("s  ");
    else
        printf("u  ");
    pb = mem[dispc++];

    if ((pb & 0x80) != 0)
        printf(",pc");
    if ((pb & 0x40) != 0)
        {
            if ((opcode & 2) != 0)
                printf(",s");
            else
                printf(",u");
        }
    if ((pb & 0x20) != 0)
        printf(",y");
    if ((pb & 0x10) != 0)
        printf(",x");
    if ((pb & 0x08) != 0)
        printf(",dp");
    if ((pb & 0x04) != 0)
        printf(",b");
    if ((pb & 0x02) != 0)
        printf(",a");
    if ((pb & 0x01) != 0)
        printf(",cc");
}

void distfr(void)
{                  
    disreg((mem[dispc] & 0xF0) / 0x10);
    printf(",");
    disreg(mem[dispc++] & 0xF);
}

void disreg(int r)
{
    switch(r)    
    {
        case 0: printf("d");
                break;
        case 1: printf("x");
                break;
        case 2: printf("y");
                break;
        case 3: printf("u");
                break;
        case 4: printf("s");
                break;
        case 5: printf("pc");
                break;
        case 8: printf("a");
                break;
        case 9: printf("b");
                break;
        case 10: printf("cc");
                break;
        case 11: printf("dp");
                break;
    }
}

void dispage2(void)
{
    opcode = mem[dispc++];
    if (opcode < 0x30)
    {
        opcode -= 0x20;
        disbranch();
    }
    else
    {
        page = 1;
        areg = (opcode < 0xC0);
        distwoad();
        page = 0;
    }
}

void dispage3(void)
{
    opcode = mem[dispc++];

    /* 6309 DIVD/DIVQ - see the matching check in sim09.c's page3(). */
    if (opcode < 0xC0 && ((opcode & 0x0F) == 0x0D || (opcode & 0x0F) == 0x0E))
    {
        printf((opcode & 0x0F) == 0x0D ? "divd" : "divq");
        if (opcode == 0x8D)
            /* DIVD immediate: instadrm[0x8D] wrongly says relative-short
               (that byte means BSR on page 0), and the divisor is a
               single byte, not the word disadrmode()'s heuristic would
               print for opcode nibble D - so print it directly. */
            printf("   #$%02X", mem[dispc++]);
        else
            disadrmode();
        return;
    }

    page=2;
    areg = (opcode < 0xC0);
    distwoad();
    page=0;

}

void do_dis(void)
{
    char key;
    dispc=getadr();
    do
    {
        prtmsg("\n");
        dis();
        fflush(stdout);
        key = getchr();
        /* Space advances to next instruction.
           Enter / any other key exits. */
    }
    while((unsigned char)key == ' ');
}

        
unint16 dis(void)
{  
    printf("%04X   ",dispc);
    fflush(stdout);
    switch(instruc[opcode = mem[dispc++]])
    {
        case 0: printf("Undefined");
                break;
        case 1: disonead();
                break;
        case 2: areg=1;
                distwoad();
                break;
        case 3: areg=0;
                distwoad();
                break;
        case 4: areg=1;
                disimpl();
                break;
        case 5: areg=0;
                disimpl();
                break;
        case 6: disbranch();
                break;
        case 7: disbranch();  /* treated by short branch routine */
                break;
        case 8: printf("psh");
                dispshpul();
                break;
        case 9: printf("pul");
                dispshpul();
                break;
        case 10: dispage2();
                break;
        case 11: dispage3();
                break;
        case 12: areg=opcode & 3;
                if (areg == 2)
                  areg = 3;
                else
                  if (areg == 3)
                     areg = 2;
                printf("lea");
                disix(areg);
                disadrmode();
                break;
	case 13: disaddr = (mem[dispc] * 0x100 + mem[dispc + 1] + dispc + 2) & 0xffff;
                 printf("lbra  $%04X",disaddr);
                 dispc += 2;
                 break;
	case 14: disaddr = (mem[dispc] * 0x100 + mem[dispc + 1] + dispc + 2) & 0xffff;
                 printf("lbsr  $%04X",disaddr);
                 dispc += 2;
                 break;
        case 15: printf("daa");
                 break;
        case 16: printf("orcc  %02X",mem[dispc++]);
                 break;
        case 17: printf("andcc %02X",mem[dispc++]);
                 break;
        case 18: printf("sec");
                 break;
        case 19: printf("exg   ");
                 distfr();
                 break;
 /*tfr*/ case 20: printf("tfr   ");
                  distfr();
                 break;
/*rts*/ case 21: printf("rts");
                 break;
/*abx*/ case 22: printf("abx");
                 break;
/*rti*/ case 23: printf("rti");
                 break;
/*cwa*/ case 24: printf("cwai");
                 break;
/*mul*/ case 25: printf("mul");
                 break;
/*swi*/ case 26: printf("swi");
                 break;
/*sw2*/ case 27: printf("swi2");
                 break;
/*sw3*/ case 28: printf("swi3");
                 break;
/*nop*/ case 29: printf("nop");
                 break;
/*syn*/ case 30: printf("sync");
                 break;
/*bsr*/ case 31: disaddr = dispc + mem[dispc] + 1;
                 if (mem[dispc] >= 0x80)
                    disaddr -= 0x100;
                 dispc++;
                 disaddr = disaddr & 0xffff;
                 printf("bsr   $%04X",disaddr);
                 break;
/*prt*/ case 32: printf("prtacc");
                 break;
/*get*/ case 33: printf("getacc");
                 break;
        case 34: printf("break");
                 break;
/*dly*/ case 35: printf("delay1ms");
                 break;
/*sns*/ case 36: printf("sensekey");
                 break;
/*sav*/ case 37: printf("save");
                 break;
/*lod*/ case 38: printf("load");
                 break;
    }
    return(dispc);
}


