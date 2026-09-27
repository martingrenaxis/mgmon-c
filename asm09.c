/*
 *
 * 6809 one line assembler
 *
 * Martin Gren May 14, 1989
 *
 * Aug 23, 1990: Added bra # and fcc, fcb and fdb support
 * Aug 27, 1990: Support for mgmon output format
 *
 */

#include "typedef.h"
#include "mgmon.h"
#include "asm09.h"
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
int  getnr(void);
void ignapos(void);
int egethex(void);
int expr(void);
int expr2(void);
int getixreg(void);
int getpreg(void);
int gettreg(void);
int pshpul(void);
int tfrexg(void);
void shortrel(void);
void longrel(void);
void pseudocode(void);
void mainad(int outtype);
int tstindex(int outtype);
int indexed(int outtype);
int oneasm(int outtype);
int do_ass(FILE *utf, int outtype, unint16 pc);
/* From mgmon.c */
void buffer(void);
void prtmsg(char *s);
void prtchr(char ch);
char getchr(void);
void ccadj(void);
void regs(void);

unsigned char *apos;
byte abyte[80];
unint16 value;
int16 opcnr, ln;
unint16 pc;
int contin;
int linenr;


char name[] = {"negb\0comb\0lsrb\0rorb\0asrb\0aslb\0rolb\0decb\0\
incb\0tstb\0clrb\0lslb\0\
nega\0coma\0lsra\0rora\0asra\0asla\0rola\0deca\0\
inca\0tsta\0clra\0lsla\0\
neg\0com\0lsr\0ror\0asr\0asl\0rol\0dec\0\
inc\0tst\0jmp\0clr\0lsl\0\
suba\0cmpa\0sbca\0anda\0bita\0lda\0sta\0\
eora\0adca\0ora\0adda\0\
subb\0cmpb\0sbcb\0andb\0bitb\0ldb\0stb\0\
eorb\0adcb\0orb\0addb\0\
ldd\0std\0addd\0subd\0cmpd\0\
ldx\0stx\0cmpx\0leax\0\
ldy\0sty\0cmpy\0leay\0\
ldu\0stu\0cmpu\0leau\0\
lds\0sts\0cmps\0leas\0\
bra\0brn\0bhi\0bls\0bhs\0blo\0bne\0beq\0\
bvc\0bvs\0bpl\0bmi\0bge\0blt\0bgt\0ble\0\
lbrn\0lbhi\0lbls\0lbhs\0lblo\0lbne\0lbeq\0\
lbvc\0lbvs\0lbpl\0lbmi\0lbge\0lblt\0lbgt\0lble\0\
nop\0sync\0lbra\0lbsr\0daa\0orcc\0andcc\0\
sex\0exg\0tfr\0pshs\0puls\0pshu\0pulu\0\
rts\0abx\0rti\0cwai\0mul\0swi2\0swi3\0swi\0\
bsr\0jsr\0bcc\0bcs\0\
prtacc\0getacc\0break\0delay1ms\0sensekey\0save\0load\0fcb\0fdb\0fcc\0org\0   "};

byte code [] = {0x50,0x53,0x54,0x56,0x57,0x58,0x59,0x5A,0x5C,0x5D,0x5F,0x58,
                0x40,0x43,0x44,0x46,0x47,0x48,0x49,0x4A,0x4C,0x4D,0x4F,0x48,
                0x00,0x03,0x04,0x06,0x07,0x08,0x09,0x0A,0x0C,0x0D,0x0E,0x0F,8,
                0x80,0x81,0x82,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,
                0xC0,0xC1,0xC2,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,
                0xCC,0xCD,0xC3,0x83,0x83,
                0x8E,0x8F,0x8C,0X30,
                0x8E,0x8F,0x8C,0X31,
                0xCE,0xCF,0x83,0X33,
                0xCE,0xCF,0x8C,0X32,
                0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
                0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
                0x21,0x22,0x23,0x24,0x25,0x26,0x27,
                0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
                0x12,0x13,0x16,0x17,0x19,0x1A,0x1C,
                0x1D,0x1E,0x1F,0x34,0x35,0x36,0x37,
                0x39,0x3A,0x3B,0x3C,0x3D,0x3F,0x3F,0x3F,0x8D,0x8D,
                0x24,0x25,
                1,2,5,11,20,0x18,0x15,1,2,3,4,0};

byte adtype [] = {1,1,1,1,1,1,1,1,1,1,1,1,
                  1,1,1,1,1,1,1,1,1,1,1,1,
                  2,2,2,2,2,2,2,2,2,2,2,2,2,
                  3,3,3,3,3,3,3,3,3,3,3,
                  3,3,3,3,3,3,3,3,3,3,3,
                  4,4,4,4,12,
                  4,4,4,14,
                  12,12,12,14,
                  4,4,13,14,
                  12,12,13,14,
                  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                  1,1,15,15,1,7,7,
                  1,8,8,9,9,9,9,
                  1,1,1,1,1,10,11,1,5,3,5,5,
                  1,1,1,1,1,1,1,16,16,16,16,0};


/*
 * type is defined as follows:
 *
 * 1 - instruction needs no further processing
 * 2 - implied instruction with direct, extended or indexed addr. mode
 * 3 - two operand instruction with all addr. modes
 * 4 - two operand instruction with all addr. modes, 16 bit value
 * 5 - short branch instruction
 * 6 - long branch instruction on page 2
 * 7 - immediate only instruction (andcc + orcc)
 * 8 - tfr/exg instructions
 * 9 - psh/pul instructions
 * 10 - page 2 implied swi2
 * 11 - page 3 implied swi3
 * 12 - two operand instruction with all addr. modes, 16 bit value page 2
 * 13 - two operand instruction with all addr. modes, 16 bit value page 3
 * 14 - indexed only instruction (lea)
 * 15 - long branch on standard page (1)
 * 16 - Pseudo op operations, fcc, fcb, fdb
 *
 */


int getnr(void)

{
    int tmp,exit,current;
    char *incode;

    incode = name;
    exit = current = 0;
    do
    {
        tmp = 0;
        current++;
        do
        {
            if (*incode == (*(apos + tmp) | 0x20))
            {
              tmp++;
              incode++;
              if (*incode == 0)
              {
                 apos += tmp;
                 return(current-1);
              }
            }
            else
               while (*incode != 0)
                    incode++;
        }
        while(*incode != 0);
        incode++;
    }
    while ((*(incode + 1) != 32) && (exit == 0));
    return(-1);
}

void ignapos(void)
{ 
    while(*apos == 32)
        apos++;
}

int egethex(void)

{
    int i;

    if (*apos > 0x60)
        *apos -= 0x20;
    i = *apos - '0';
    if (i < 0)
        return(16);
    if (i > 9)
        i -= 7;
    apos++;
    return(i);
}

int expr(void)

{
    if (*apos == '-')
    {
        apos++;
        return(0-expr2());
    }
    return(expr2());
}

int expr2(void)

{
    int i,val;

    val = 0;
    if (*apos == '\'')
    {
        /* Character literal, e.g. #'B - one apostrophe followed by
           the literal character, whose ASCII code is the value.
           egethex() has no notion of this prefix at all: fed the
           apostrophe itself, it rejects it as an invalid digit and
           returns without advancing apos, so without this case the
           whole expression silently parsed as 0. */
        apos++;
        val = *apos;
        apos++;
    }
    else if (*apos == '$')
    {
        apos++;
        while ((i = egethex()) < 0x10)
            val = val * 0x10 + i;
    }
    else
    {
        while ((i = egethex()) < 10)
            val = val * 10 + i;
    }
    return(val);
}

int getixreg(void)
{
    switch(*apos++)
    {
        case 'x':
        case 'X': return(0);
        case 'y':
        case 'Y': return(0x20);
        case 'u':
        case 'U': return(0x40);
        case 's':
        case 'S': return(0x60);
        case 'p':
        case 'P': return(0x8C);
        default : return(0xFF);
    }
}

int getpreg(void)

{

    if (*apos > 0x60)
        *apos -= 0x20;
    switch(*apos++)
    {
        case 'A': return(2);
        case 'B': return(4);
        case 'X': return(0x10);
        case 'Y': return(0x20);
        case 'U': return(0x40);
        case 'S': return(0x40);
        case 'D': if ((*apos == 'p') || (*apos == 'P'))
                  {
                      apos++;
                      return(8);
                   }
                   else
                      return(6);
        case 'C': apos++;
                  return(1);
        case 'P': apos++;
                  return(0x80);
        default:  return(-1);
    }
}

int gettreg(void)

{

    if (*apos > 0x60)
        *apos -= 0x20;
    switch(*apos++)
    {
        case 'A': return(8);
        case 'B': return(9);
        case 'X': return(1);
        case 'Y': return(2);
        case 'U': return(3);
        case 'S': return(4);
        case 'D': if ((*apos == 'p') || (*apos == 'P'))
                  {
                      apos++;
                      return(11);
                   }
                   else
                      return(0);
        case 'C': apos++;
                  return(10);
        case 'P': apos++;
                  return(5);
        default:  return(-1);
    }
}

int pshpul(void)
{
    int i;

    i = 0;
    do
        i |= getpreg();
    while(*apos++ == ',');
    return(i);
}

int tfrexg(void)

{
    int t;

    t = gettreg() * 0x10;
    if (*apos++ != ',')
        return(0xFF);
    t += gettreg();
    return (t);
}

void shortrel(void)
{
    if (*apos == '#')
    {
      apos++;
      value = expr();
    }
    else
    {
      value = expr() - pc - 2;
    }
    if ((value >= 0x80) && (value < 0xFF80))
        prtmsg("Relative branch too far\n");
    ln = 2;
    abyte[2] = value & 0xFF;
}

void pseudocode(void)
{
  int i, delim;

    switch(abyte[1])
    {
      case 2: /* fdb instructions */
              i = 1;
              do
              {
                value = expr();
                abyte[i++] = value >> 8;
                abyte[i++] = value & 0xff;
              }
              while(*apos++ == ',');
              ln = i - 1;
              break;
      case 3: /* fcc instructions */
              i = 1;
              delim = *apos++;
              if (3 == 4) printf("delim is %02x\n", delim);
              while ((*apos != delim) && (*apos != '\n') && (*apos != 13))
              {
                abyte[i++] = *apos++;
              }
              if (*apos == delim)
              {
                 apos++;
                 ignapos();
                 if (*apos++ == ',')
                 {
                    do
                    {
                      value = expr();
                      abyte[i++] = value & 0xff;
                    }
                    while(*apos++ == ',');
                 }
              }
              ln = i - 1;
              break;
      case 1: /* fcb instructions */
              i = 1;
              do
              {
                value = expr();
                abyte[i++] = value & 0xff;
              }
              while(*apos++ == ',');
              ln = i - 1;
              break;
      case 4: /* org instructions */
              pc = expr();
              inbuf[1] = pc >> 8;
              inbuf[2] = pc & 0xff;
              ln = 0;
              contin = (1 == 1);
              break;
    }
    if (3 == 4) printf("got %d bytes\n", ln);
}

void longrel(void)
{
    if (*apos == '#')
    {
      apos++;
      value = expr();
    }
    else
    {
      value = expr() - pc - 4;
      if (adtype[opcnr] == 15)
          value++;
    }
    ln = 3;
    abyte[2] = value / 0x100;
    abyte[3] = value & 0xFF;
}

int tstindex(int outtype)

/* return 1 if it was indexed, otherwise 0 */

{
    (void)outtype;  /* parameter unused but kept for consistent signature */
    unsigned char *tmp, *tmp2;

    if ((*apos == '[') || (*apos == '{'))
        return(1);
    if (*(apos + 1) == ',')
        return (1);
    if (*apos == ',')
        return(1);
    tmp = apos;
    value = expr();
    tmp2 = apos;
    apos = tmp;
    if (*tmp2 == ',')
        return(1);
    else
        return(0);
}

int indexed(int outtype)

/* This function returns 0 if it is successfully parsed.
   otherwise it will give the error number */

{
    abyte[1] |= 0x20;

    abyte[2] = 0;
    if ((*apos == '[') || (*apos == '{'))
    {
        apos++;
        abyte[2] = 0x10;
    }
    ln = 2;
    if (*(apos + 1) == ',')
    {
        switch(*apos)
        {
            case 'a':
            case 'A': abyte[2] |= 0x86;
                      break;
            case 'b':
            case 'B': abyte[2] |= 0x85;
                      break;
            case 'D':
            case 'd': abyte[2] |= 0x8B;
        }
        if (abyte[2] > 0x80)
        {
            apos += 2;
            abyte[2] |= getixreg();
            ln = 2;
            return(0);
        }
    }
    value = expr();
    if (*apos++ != ',')
    {
        if (abyte[2] == 0x10)
        {
            abyte[2] |= 0x8F;          /* extended indirect */
            abyte[3] = value / 0x100;
            abyte[4] = value & 0xFF;
            ln = 4;
            return(0);
        }
        return(128);  /* strange indexed instr. */
    }
    if ((value == 0) && (*apos != 'p') && (*apos != 'P'))
    /* treat autoincrement/decr. + zero offs. */
    {
        if(*apos == '-')
        {
            apos++;
            if(*apos == '-')
            {
                    abyte[2] |= 0x83;
                    apos++;
            }
            else
               abyte[2] |= 0x82;
            abyte[2] |= getixreg();
            return(0);
        }
        if (*(apos + 1) == '+')
        {
            abyte[2] |= 0x80 + getixreg();
            if (*(apos + 1) == '+')
                abyte[2]++;
            return(0);
        }
        abyte[2] |= 0x84 + getixreg();
        return(0);
    }
    abyte[2] |= getixreg();
    if ((abyte[2] & 0xF) == 0xC)     /* Treat pc relative indexed */
    {
        value -= pc + 3;
        if ((value >= 0x80) && (value < 0xFF80))
        {
            if (outtype == MEMORY)
               prtmsg("PC long\n");
            ln = 4;
            abyte[2]++;
            value--;
            abyte[3] = value / 0x100;
            abyte[4] = value & 0xFF;
            return(0);
        }
        else
        {
            if (outtype == MEMORY)
               prtmsg("PC short\n");
            ln = 3;
            abyte[3] = value & 0xFF;
            return(0);
        }
    }
    if (((value < 16) || (value > 0xFFEF)) && ((abyte[2] & 0x10) == 0))
    {
        abyte[2] |= value & 0x1F;
        return(0);
    }
    if ((value < 0x80) | (value > 0xFF7F))
    {
        abyte[2] |= 0x88;
        abyte[3] |= value & 0xFF;
        ln = 3;
        return(0);
    }
    abyte[2] |= 0x89;
    abyte[3] = value / 0x100;
    abyte[4] = value & 0xFF;
    ln = 4;
    return(0);
}


void mainad(int outtype)
{
    switch(*apos)
    {
        case '<': apos++;
                  abyte[2] = expr() & 0xFF;
                  ln = 2;
                  abyte[1] |= 0x10;
                  break;
        case '#': apos++;
                  ln = 2;
                  if ((adtype[opcnr] == 3) | (adtype[opcnr] == 7))
                      abyte[2] = expr() & 0xFF;
                  else
                  {
                      value = expr();
                      abyte[2] = value / 0x100;
                      abyte[3] = value & 0xFF;
                      ln = 3;
                  }
                  break;
        case '>': apos++;
                  value = expr();
                  abyte[2] = value / 0x100;
                  abyte[3] = value & 0xFF;
                  ln = 3;
                  abyte[1] |= 0x30;
                  break;
        case 13:  ln = 1;
                  break;
        default:  if(tstindex(outtype))  /* should set value */
                    indexed(outtype);
                  else
                  {
                    abyte[2] = value / 0x100;
                    abyte[3] = value & 0xFF;
                    abyte[1] |= 0x30;
                    ln = 3;
                  }
    }
}


int oneasm(int outtype)
{
    int p_type;

    ln = abyte[1] = abyte[2] = abyte[3] = abyte[4] = abyte[5] = 0;
    apos = &inbuf[pos];
    ignapos();
    if (*apos == '*')
    {
       ln = 0;
       inbuf[1] = pc >> 8;
       inbuf[2] = pc & 0xff;
       contin = (1 == 1);
       return(0);
    }
    opcnr = getnr();
    if (opcnr == -1)
    {
        printf("Illegal opcode at line %d\r\n",linenr);
        return(0);
    }
    abyte[1] = code[opcnr];
    ln = 1;
    ignapos();
    switch(adtype[opcnr])
    {
        case 1:
        case 10:
        case 11: break;
        case 12:
        case 13:
        case 3:
        case 4:
        case 7: mainad(outtype);
                break;
        case 2: mainad(outtype);
                if ((abyte[1] > 0x0F) && (abyte[1] < 0x20))
                    abyte[1] &= 0x0F;
                if ((abyte[1] > 0x1F) && (abyte[1] < 0x40))
                    abyte[1] |= 0x40;
                break;
        case 14:indexed(outtype);
                break;
        case 8 :ln = 2;
                abyte[2] = tfrexg();
                break;
        case 9: ln = 2;
                abyte[2] = pshpul();
                break;
        case 5: shortrel();
                break;
        case 6: longrel();
                break;
        case 15:longrel();
                break;
        case 16:pseudocode();
                break;
        default:prtmsg("Undefined addresing mode\n");
                ln = 0;
                break;
    }
    p_type = 0x10;
    switch(adtype[opcnr])
    {
       case 11:
       case 13: p_type = 0x11; /* fall through */
       case 6:
       case 10:
       case 12: abyte[5] = abyte[4];
                abyte[4] = abyte[3];
                abyte[3] = abyte[2];
                abyte[2] = abyte[1];
                abyte[1] = p_type;
                ln++;
    }
    p_type = 0;
    return(ln);
}

void assemble(FILE *utf, int outtype)
{
    linenr = pc = 0;
    do
    {
       contin = (1 == 0);
       buffer();
       if (DEBUG) printf("Line: %d addr %04x\r\n", linenr, pc);
       do_ass(utf, outtype, pc);
       pc += ln;
       if (contin)
          pc = inbuf[1] * 0x100 + inbuf[2];
    }
    while ((ln != 0) || (contin));
}

int do_ass(FILE *utf, int outtype, unint16 curpc)
{
    int i;

    /*
     * oneasm()/mainad() compute PC-relative branch offsets (shortrel(),
     * longrel()) against the file-scope global `pc` - NOT this
     * parameter. assemble()'s own loop keeps that global correctly in
     * sync as it advances, but do_asm()'s interactive "ass" command
     * (mgmon.c) tracks the current address in its own local variable
     * and just passes it in here, leaving the global at whatever it
     * last was (0 if never touched this run). That mismatch made
     * every branch assembled via the interactive "ass" command compute
     * its offset against the wrong base address - harmless for
     * forward-only code, but silently wrong for any bra/beq/bne/etc.
     * Sync it here so both callers agree on the current address.
     */
    pc = curpc;

    oneasm(outtype);
    linenr++;
    if ((outtype == MGMON) && (ln >= 1))
       fprintf(utf, "p.%04x\n", curpc);
    inbuf[0] = ln;
    for (i = 1; i <= ln; i++)
        switch(outtype)
        {
          case SIM:    fprintf(utf, "SETV M[%d] = %d\n", curpc++, abyte[i]);
                       break;
          case MGMON:  fprintf(utf, "%02x", abyte[i]);
                       curpc++;
                       if (DEBUG) printf("%04x: %02x\r\n", curpc, abyte[i]);
                       break;
          case MEMORY: inbuf[i + 3] = abyte[i];
                       break;
          case BINARY: fprintf(utf, "%c", abyte[i]);
                       break;
          case AX7PARAM: fprintf(utf, "%02X", abyte[i]);
                       break;
          default:     prtmsg("Unknown output type\n");
                       exit(1);
                       break;
        }
    if ((outtype == MGMON) && (ln >= 1))
       fprintf(utf, "\n.\n");
    return (ln);
}
