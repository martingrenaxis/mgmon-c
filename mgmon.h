
#define CR 10
#define MAXSIZE 0x20000

extern byte mem[];
extern byte accu[5],page,aslask,atyp,opcode,ityp,reg,areg;
extern byte acce,accf;   /* 6309 W register (E:F) - see sim09.c, used by DIVQ */
extern unint16 dispc,pc;
extern byte src,dst,leareg,bryt,autostop;
extern byte inbuf[];
extern int mode_i;
extern int16 pos;
extern byte instadrm[], instruc[];
extern int mode_s,mode_r,mode_i,mode_p,mode_g,addr,break_pc, mode_sim;
extern int break_g,mode_q,break_qpc;

/* ADM3A terminal-emulation state machine (see adm3a_putc() in mgmon.c) -
   shared between do_term()'s serial byte stream and sim09.c's "prt"
   pseudo-instruction, so a simulated program's console output gets the
   same clear-screen/cursor-address translation as the real board's. */
enum { ADM3A_NORMAL, ADM3A_ESC, ADM3A_ESC_EQ, ADM3A_ESC_EQ_ROW };
void adm3a_putc(int *state, int *row, unsigned char c);



