/*
 * MG monitor. Copyright Martin Gren Sep 18, 1987
 *
 * The monitor was originally written in 6809 assembly back in
 * 1981. This is a port of the assembly code to C. The program
 * will also compile under MS-LOSS.
 *
 * Apr 15, 1990 MG: Included 6809 simulator.
 * update: Aug 23, 1990
 * update: July 5, 2026: Vibe coded for many new functions. Flex file read and management.
 * update: July 29, 2026: help <topic> (s1/flex/6809), s1save command.
 * Last update: July 31, 2026: flexinject/fdelete/frename - FLEX disk image write
 *   support, plus a "debug on|off" trace of every disk write (on by default).
 *
 * This monitor may be extended to support other commands etc.
 *
 * If this is done, it must maintain the general syntax as defined here.
 *
 * If you do any changes/improvements, please make a log in this file.
 * All suggestions for other improvements may be mailed to martin@axis.se
 *
 */


#include    "typedef.h"
#include    "mgmon.h"
#include    "asm09.h"
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <termios.h>
#include    <unistd.h>
#include    <signal.h>
#include    <ctype.h>
#include    <time.h>
#include    <dirent.h>
#include    <fcntl.h>
#include    <sys/select.h>
#include    <sys/ioctl.h>
#include    <IOKit/serial/ioss.h>

/*  #nclude    <ipc.h>  If you want to use system 5 memory */

#define     maxtkn        50

/*
 *
 * This variable is defined statically for the whole package.
 * If used in a target system, it should be the full system memory.
 *
 */

byte mem[MAXSIZE];
byte inbuf[80];

extern unint16 ix[7],ixslask,ixslask2;
extern unint16 pc;
int addr,break_pc,break_qpc,break_g;
int DEBUG;

/* 6809 status flags: */

extern int c,v,n,z;

/* mode register bits */

int mode_p,mode_tr,mode_s,mode_r,mode_i,mode_g,mode_q, mode_sim;

/* mode_p = break on pc=        */
/* mode_q = break on pc=        */
/* mode_tr = trace mode on       */
/* mode_s = single step mode on */
/* mode_r = print registers     */
/* mode_i = print instructions  */
/* mode_g = go for instructions */
/* mode_sim = output assembler data so it fits the sim command */




/*
 *
 * All procedures are stored here for c purpose
 *
 */

/* Forward declarations for functions in this file */
void prtmsg(char *string);
void prtchr(char ch);
char getchr(void);
int  check_esc(void);
void prtadr(int32 adr);
void prthex(byte hex);
void prtnib(byte nib);
void buffer(void);
void lower(void);
int  ignblk(void);
int  sekcom(void);
int  nxtch(void);
int  gethex(void);
int  getbyt(void);
int32 getadr(void);
void scancr(void);
void regs(void);
void ccadj(void);
int  not(int n);
void shell(void);

/* Forward declarations for functions in other files */
void do_list(void);
void do_info(void);
void do_prog(void);
void do_move(void);
void do_exit(void);
void do_verf(void);
void do_check(void);
void do_fill(void);
void do_find(void);
void do_modify(void);
void do_load(void);
void do_save(void);
void do_run(void);
void do_dis(void);
void do_reg(void);
void do_set(void);
void do_mode(void);
void do_peps(void);
void do_help(void);
void do_reset(void);
void do_asm(void);
int  do_ass(FILE *utf, int outtype, unint16 pc);
void do_s1load(void);
void do_s1send(void);
void do_s1save(void);
void do_sys(void);
void do_dskfile(void);
void do_fcat(void);
void do_flist(void);
void do_fsave(void);
void do_sector(void);
void do_fget(void);
void do_flex2file(void);
void do_term(void);
void do_s1port(void);
void do_sendfile(void);
void do_flexinject(void);
void do_fdelete(void);
void do_frename(void);
void do_debug(void);
void do_putsector(void);
void do_exec(void);
void dispatch(int funktion);
/* Terminal raw mode */
static void term_raw(void);
static void term_restore(void);
static void hist_load(void);
static void hist_save(void);


struct   comtype
{
  char   *name;
  int    command_num;
};



#define MAXCOMMAND  41
struct comtype  commands[MAXCOMMAND]=
                  {
                    {"list",1},      {"info",2},
                    {"program",3},   {"move",4},
                    {"exit",5},      {"fill",6},
                    {"verify",7},    {"check",8},
                    {"find",9},      {"modify",10},
                    {"load",11},      {"save",12},
                    {"reg",13},        {"set",14},
                    {"run",15},        {"dis",16},
                    {"mode",17},      {"peps",18},
                    {"help",19},      {"reset",20},
                    {"ass",21},       {"s1load",22},
                    {"s1send",23},    {"sys",24},
                    {"dskfile",25},   {"fcat",26},
                    {"flist",27},     {"fsave",28},
                    {"sector",29},   {"fget",30},
                    {"flex2file",31}, {"term",32},
                    {"s1port",33},    {"sendfile",34},
                    {"s1save",35},    {"flexinject",36},
                    {"fdelete",37},   {"frename",38},
                    {"debug",39},     {"putsector",40},
                    {"exec",41}
                  };

int16 pos;
FILE *fil;

/*
 *
 * FLEX 6809 disk image state, remembered across dskfile/fcat commands.
 *
 * Layout reference (verified against a working FLEX disk-image parser):
 *
 *   Sector size is always 256 bytes. A (track,sector) pair maps to a byte
 *   offset as ((track * sectors_per_track) + (sector - 1)) * 256 - sector
 *   numbers start at 1 on every track, including track 0.
 *
 *   System Information Record (SIR) lives at track 0, sector 3:
 *     offset 16-26  volume label (11 bytes)
 *     offset 29-30  first data track/sector
 *     offset 31-32  last data track/sector
 *     offset 33-34  free sector count (16-bit big-endian)
 *     offset 38     highest track number
 *     offset 39     highest sector number (sectors per track)
 *
 *   The directory is a linked chain of sectors starting at track 0,
 *   sector 5. Each directory sector holds 10 entries of 24 bytes each,
 *   starting at offset 16; bytes 0-1 of the sector are the track/sector
 *   of the next directory sector (0,0 marks the end of the chain).
 *
 *   Directory entry (24 bytes):
 *     offset 0-7    filename (padded with spaces or 0x00 - both occur
 *                   on real images, flex_pad_eq() treats them as equal)
 *     offset 8-10   extension (padded the same way)
 *     offset 13-14  start track/sector
 *     offset 15-16  end track/sector
 *     offset 17-18  total sectors used (16-bit big-endian)
 *     offset 21-23  creation date (month, day, year)
 *   An entry is unused/deleted when its first byte is 0x00 or has the
 *   high bit set.
 *
 */

#define FLEX_SECSZ  256

static FILE *dsk_fil = NULL;
static char  dsk_name[256];
static int   dsk_max_track;
static int   dsk_max_sector;
static int   dsk_readonly;
static char  dsk_label[12];
static int   dsk_last_track  = -1;   /* last sector shown by sector, and */
static int   dsk_last_sector = -1;   /* its own chain-link field (bytes 0-1), */
static int   dsk_last_link_t = -1;   /* used by the no-argument "next sector" */
static int   dsk_last_link_s = -1;   /* form to follow the FLEX chain link */

/* On by default: every FLEX disk write (flexinject, fdelete, frename,
   fsave) prints the (track,sector) and the sector's first 4 bytes
   (the chain-link/record-number header) right before writing it, so
   a write that goes wrong is visible immediately rather than only
   discoverable afterwards. See do_debug() / "debug on|off". */
static int   flex_debug = 1;


int main(void)

{

  mode_p = mode_tr = mode_s = mode_r = mode_i = (1 == 0);
  mode_g = mode_q =  mode_sim = (1 == 0);

/*
 *
 * This section is if you want to use system 5 shared memory procedures.
 *
 * int id, key;
 * init();
 * key = 6809;
 * if ((id = shmget(key, size, 0)) < 0)
 * if ((id = shmget(key, size, 0666 | IPC_CREAT)) < 0)
 * {
 * perror("shmget");
 * exit(1);
 * }
 * mem = shmat(id, 0, 0);
 *
 */

  do_reset();  /* initiate CPU registers   */
  pc = 0;
  autostop = 0;

/*
 *
 * The system calls are to make it character oriented on a Unix system.
 * If you port it into a target system, they shall be excluded.
 * Cut the lines together for Unix compatibility.
 *
 */

     hist_load();
     term_raw();
     shell();
     term_restore();
     hist_save();
     prtmsg("\nMG-monitor exited.\n");
     exit(0);
}

/* ---------------------------------------------------------- */

void shell(void)
{
int funktion;

   funktion = 7;
   prtmsg("\nMG monitor\n4.1 - compiled " __DATE__ " " __TIME__ "\n\nType help if in doubt!\n");
   do
   {
     if (funktion != 0)
        prtmsg("\nMgmon\n");
     buffer();
     lower();
     funktion = sekcom();
     if (funktion == -1)
       prtmsg("Undefined command, try help!\n");
     if ((funktion > 0) && (funktion <= MAXCOMMAND))
        dispatch(funktion);
 } while(funktion != 5);
}

/*
 *
 * Run the do_* handler for a command number resolved by sekcom() (1..
 * MAXCOMMAND). Factored out of shell()'s main loop so do_exec() can
 * run commands read from a file through the exact same dispatch a
 * typed command goes through, rather than duplicating this switch.
 *
 */

void dispatch(int funktion)
{
      switch(funktion)
      {
      case 1:    do_list();
                 break;
      case 2:    do_info();
                 break;
      case 3:    do_prog();
                 break;
      case 4:    do_move();
                 break;
      case 5:    do_exit();
                 break;
      case 6:    do_fill();
                 break;
      case 7:    do_verf();
                 break;
      case 8:    do_check();
                 break;
      case 9:    do_find();
                 break;
      case 10:    do_modify();
                 break;
      case 11:   do_load();
                 break;
      case 12:   do_save();
                 break;
      case 13:   do_reg();
                 break;
      case 14:   do_set();
                 break;
      case 15:   do_run();
                 break;
      case 16:   do_dis();
                 break;
      case 17:   do_mode();
                 break;
      case 18:   do_peps();
                 break;
      case 19:   do_help();
                 break;
      case 20:   do_reset();
                 break;
      case 21:   do_asm();
                 break;
      case 22:   do_s1load();
                 break;
      case 23:   do_s1send();
                 break;
      case 24:   do_sys();
                 break;
      case 25:   do_dskfile();
                 break;
      case 26:   do_fcat();
                 break;
      case 27:   do_flist();
                 break;
      case 28:   do_fsave();
                 break;
      case 29:   do_sector();
                 break;
      case 30:   do_fget();
                 break;
      case 31:   do_flex2file();
                 break;
      case 32:   do_term();
                 break;
      case 33:   do_s1port();
                 break;
      case 34:   do_sendfile();
                 break;
      case 35:   do_s1save();
                 break;
      case 36:   do_flexinject();
                 break;
      case 37:   do_fdelete();
                 break;
      case 38:   do_frename();
                 break;
      case 39:   do_debug();
                 break;
      case 40:   do_putsector();
                 break;
      case 41:   do_exec();
                 break;
      }
}


/*
 *
 * All memory read and write should use the following routines.
 * This is to allow for units where memory is not strightforward
 *
 * plesae use these routines rather than reading directly in the mem
 * variable.
 *
 * This is not maintained by the 6809 simulator due to performance
 * degradation.
 *
 */

char memr(int32 adr)
{
  return(mem[adr]);
}

void memw(int32 adr, int data)
{
  mem[adr] = data;
}

/*
 *
 * Terminal raw mode using termios - works reliably on macOS and Linux.
 * Replaces the old system("stty cbreak -echo") approach.
 *
 * term_raw()    - put stdin into raw mode (no echo, char-at-a-time,
 *                 no special key processing, no CR/NL translation)
 * term_restore()- restore original settings
 *
 */

static struct termios term_saved;
static int term_is_raw = 0;

static void term_restore(void)
{
    if (term_is_raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_saved);
        term_is_raw = 0;
    }
}

/*
 * Toggle ICRNL on stdin without touching anything else currently in
 * effect. term_raw() leaves ICRNL on so the kernel maps the user's
 * real CR ($0D) to LF ($0A), matching this monitor's own CR==10
 * convention (mgmon.h) - but a guest 6809 program running under the
 * simulator (FLEX, etc.) expects a genuine $0D from its own console
 * input routine, since that's the real hardware convention it's
 * written against. do_run() (sim09.c) calls this with 0 for the
 * duration of a run so "get" (opcode 2/case 33) delivers Enter
 * exactly as typed, and with 1 afterwards so mgmon's own prompt goes
 * back to working exactly as before.
 */
void term_set_icrnl(int on)
{
    struct termios tio;

    if (!isatty(STDIN_FILENO))
        return;
    tcgetattr(STDIN_FILENO, &tio);
    if (on)
        tio.c_iflag |= ICRNL;
    else
        tio.c_iflag &= ~ICRNL;
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
}

static void term_raw(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO)) return;   /* running in a pipe - skip */
    tcgetattr(STDIN_FILENO, &term_saved);
    atexit(term_restore);                /* safety net on any exit   */

    raw = term_saved;
    /* Input flags: no break signal, no parity check, no strip, no flow ctrl.
       ICRNL is LEFT ON so Return (0x0D) is mapped to LF (0x0A) by the kernel,
       keeping CR=10 valid everywhere in the existing command parser. */
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
    /* Output flags: disable all post-processing so we have full control
       over line endings. prtchr('\n') emits \r\n explicitly. */
    raw.c_oflag &= ~(OPOST);
    /* Control flags: 8-bit chars */
    raw.c_cflag |=  (CS8);
    /* Local flags: no echo, no canonical mode, no signals from keys.
       IEXTEN is LEFT ON so that 0x7F (the Mac rubout/backspace key)
       is delivered correctly to read(). */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    /* read() returns as soon as 1 byte is available */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    term_is_raw = 1;
}

void prtchr(char ch)
{
    /* In raw mode the terminal does not translate \n to CR+LF for us,
       so we emit CR+LF ourselves whenever we want a newline. */
    if (ch == '\n') {
        write(STDOUT_FILENO, "\r\n", 2);
    } else {
        write(STDOUT_FILENO, &ch, 1);
    }
}

#define CTRL_A  0x01
#define CTRL_B  0x02
#define CTRL_E  0x05
#define CTRL_F  0x06
#define CTRL_G  0x07
#define CTRL_K  0x0B
#define CTRL_N  0x0E
#define CTRL_P  0x10
#define CTRL_R  0x12
char getchr(void)
{
    unsigned char c = 0;
    read(STDIN_FILENO, &c, 1);

    /* Translate VT100/xterm arrow-key escape sequences to Emacs-style
     * control characters so the existing line-editor handles them without
     * any other changes.
     *
     * Arrow sequences:  ESC [ A  = Up    -> Ctrl-P (previous history)
     *                   ESC [ B  = Down  -> Ctrl-N (next history)
     *                   ESC [ C  = Right -> Ctrl-F (forward char)
     *                   ESC [ D  = Left  -> Ctrl-B (backward char)
     *
     * Home/End (common extras):
     *                   ESC [ H  = Home  -> Ctrl-A (start of line)
     *                   ESC [ F  = End   -> Ctrl-E (end of line)
     *
     * We use a non-blocking second read (VMIN=0, VTIME=0) to peek at the
     * next byte without stalling if ESC was pressed on its own.
     */
    if (c == 0x1B)          /* ESC */
    {
        unsigned char c2 = 0, c3 = 0;
        struct termios peek;
        tcgetattr(STDIN_FILENO, &peek);
        peek.c_cc[VMIN]  = 0;   /* non-blocking */
        peek.c_cc[VTIME] = 1;   /* 100 ms timeout */
        tcsetattr(STDIN_FILENO, TCSANOW, &peek);
        if (read(STDIN_FILENO, &c2, 1) == 1 && c2 == '[')
        {
            if (read(STDIN_FILENO, &c3, 1) == 1)
            {
                switch (c3)
                {
                    case 'A': c = CTRL_P; break;   /* Up    -> Ctrl-P */
                    case 'B': c = CTRL_N; break;   /* Down  -> Ctrl-N */
                    case 'C': c = CTRL_F; break;   /* Right -> Ctrl-F */
                    case 'D': c = CTRL_B; break;   /* Left  -> Ctrl-B */
                    case 'H': c = CTRL_A; break;   /* Home  -> Ctrl-A */
                    case 'F': c = CTRL_E; break;   /* End   -> Ctrl-E */
                    default:  c = 0;      break;   /* unknown: discard */
                }
            }
        }
        /* else: lone ESC or unrecognised sequence - discard (return 0) */
        peek.c_cc[VMIN]  = 1;
        peek.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &peek);
    }

    return (char)c;
}

/*
 * Non-blocking check for a pending ESC keypress on stdin, used by
 * do_run() so a running simulation can be interrupted without needing
 * single-step mode. Returns 1 if ESC was waiting, 0 otherwise (whether
 * nothing was pending or some other key was pressed - either way, at
 * most one byte is consumed from the input stream).
 */

int check_esc(void)
{
    unsigned char c = 0;
    struct termios peek;
    int got;

    tcgetattr(STDIN_FILENO, &peek);
    peek.c_cc[VMIN]  = 0;   /* non-blocking */
    peek.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &peek);

    got = (read(STDIN_FILENO, &c, 1) == 1) && (c == 0x1B);

    peek.c_cc[VMIN]  = 1;   /* restore normal blocking reads */
    peek.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &peek);

    return got;
}

/*
 * Non-blocking keyboard sense for the simulator's "sensekey" pseudo-
 * instruction: same poll-and-restore pattern as check_esc(), but
 * returns the actual key code instead of discarding anything that
 * isn't ESC. Returns 0 if no key is currently waiting - never blocks.
 */

int sensekey(void)
{
    unsigned char c = 0;
    struct termios peek;
    int got;

    tcgetattr(STDIN_FILENO, &peek);
    peek.c_cc[VMIN]  = 0;   /* non-blocking */
    peek.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &peek);

    got = (read(STDIN_FILENO, &c, 1) == 1);

    peek.c_cc[VMIN]  = 1;   /* restore normal blocking reads */
    peek.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &peek);

    return got ? (int)c : 0;
}

void prtmsg(char *string)
{
  int i;

  for(i = 0;string[i] != 0;)
  {
    if (string[i++] == '\n')
       prtchr('\n');
    else
       prtchr(string[i - 1]);
  }
}

/*
 *
 * Command history - ring buffer of the last HIST_SIZE commands.
 *
 * Navigation (readline-style ctrl keys):
 *   Ctrl-P  (^P, 0x10)  previous command (like Up arrow in bash)
 *   Ctrl-N  (^N, 0x0E)  next command / back to draft
 *   Ctrl-F  (^F, 0x06)  move cursor forward one character
 *   Ctrl-B  (^B, 0x02)  move cursor backward one character
 *   Ctrl-A  (^A, 0x01)  move cursor to start of line
 *   Ctrl-E  (^E, 0x05)  move cursor to end of line
 *   Ctrl-K  (^K, 0x0B)  kill (erase) from cursor to end of line
 *   Ctrl-R  (^R, 0x12)  emacs-style reverse incremental search
 *   Backspace / ^H      delete character before cursor
 *   Delete (0x7F)       delete character before cursor
 */

#define HIST_SIZE  25
#define HIST_LEN   (maxtkn + 2)
#define HIST_FILE  ".mgmoncommands"   /* saved in the current directory */


static char hist[HIST_SIZE][HIST_LEN];
static int  hist_count = 0;

static void hist_push(const char *line)
{
    int prev;
    if (!line || line[0] == '\0') return;
    if (hist_count > 0) {
        prev = (hist_count - 1) % HIST_SIZE;
        if (strncmp(hist[prev], line, HIST_LEN - 1) == 0) return;
    }
    strncpy(hist[hist_count % HIST_SIZE], line, HIST_LEN - 1);
    hist[hist_count % HIST_SIZE][HIST_LEN - 1] = '\0';
    hist_count++;
}

static const char *hist_get(int offset)
{
    int avail = (hist_count < HIST_SIZE) ? hist_count : HIST_SIZE;
    if (offset < 1 || offset > avail) return NULL;
    return hist[(hist_count - offset) % HIST_SIZE];
}

/* Case-insensitive substring search over history, most-recent first.
 * Starts at hist_get()'s offset numbering (1 = most recent) so a
 * caller can resume a search past a previous match by passing
 * start_offset+1. Returns the offset of the first match at or after
 * start_offset, or 0 if there is none. */
static int hist_find(const char *query, int start_offset)
{
    int avail = (hist_count < HIST_SIZE) ? hist_count : HIST_SIZE;
    int off, i;
    const char *line;
    char qlow[HIST_LEN], llow[HIST_LEN];

    if (query[0] == '\0') return(0);
    for (i = 0; query[i] != '\0' && i < HIST_LEN - 1; i++)
        qlow[i] = (char)tolower((unsigned char)query[i]);
    qlow[i] = '\0';

    for (off = start_offset; off <= avail; off++)
    {
        line = hist_get(off);
        if (line == NULL) break;
        for (i = 0; line[i] != '\0' && i < HIST_LEN - 1; i++)
            llow[i] = (char)tolower((unsigned char)line[i]);
        llow[i] = '\0';
        if (strstr(llow, qlow) != NULL) return(off);
    }
    return(0);
}

/* Load command history from HIST_FILE.
 * Reads up to HIST_SIZE lines; silently ignores a missing file. */
static void hist_load(void)
{
    FILE *fp;
    char line[HIST_LEN];
    size_t n;

    fp = fopen(HIST_FILE, "r");
    if (!fp) return;

    while (fgets(line, (int)sizeof(line), fp)) {
        /* Strip trailing newline / CR */
        n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
            line[--n] = '\0';
        if (n > 0)
            hist_push(line);
    }
    fclose(fp);
}

/* Save command history to HIST_FILE.
 * Writes the oldest-first so that hist_load() replays them in order,
 * ending with the most-recent command at the top of the history stack. */
static void hist_save(void)
{
    FILE *fp;
    int   avail, i, idx;

    avail = (hist_count < HIST_SIZE) ? hist_count : HIST_SIZE;
    if (avail == 0) return;

    fp = fopen(HIST_FILE, "w");
    if (!fp) {
        perror(HIST_FILE);
        return;
    }

    /* oldest entry first */
    for (i = avail; i >= 1; i--) {
        idx = (hist_count - i) % HIST_SIZE;
        fprintf(fp, "%s\n", hist[idx]);
    }
    fclose(fp);
}

/* ------------------------------------------------------------------ */

void buffer(void)
{
    char        linebuf[HIST_LEN]; /* line being edited                  */
    char        saved[HIST_LEN];   /* draft saved when browsing history  */
    int         len;               /* current length of content          */
    int         cursor;            /* insert position (0..len)           */
    int         hist_pos;          /* 0=draft, 1=most recent, ...        */
    char        ch;
    int         i;

    memset(linebuf, 0, sizeof(linebuf));
    memset(saved,   0, sizeof(saved));
    len = cursor = hist_pos = 0;

    for (;;)
    {
        ch = getchr();

        /* ---- Return / Enter ---- */
        if (ch == CR || (unsigned char)ch == 13)
        {
            prtchr('\n');
            linebuf[len] = '\0';
            hist_push(linebuf);
            /* Write into inbuf in the legacy format expected by sekcom() */
            memcpy(inbuf, linebuf, len);
            inbuf[len]   = CR;
            inbuf[len+1] = CR;
            pos = 0;
            return;
        }

        /* ---- +/- on first char: immediate execute (legacy) ---- */
        if (len == 0 && (ch == '+' || ch == '-'))
        {
            prtchr(ch);
            prtchr('\n');
            inbuf[0] = ch;
            inbuf[1] = CR;
            inbuf[2] = CR;
            pos = 0;
            return;
        }

        /* ---- Ctrl-P: previous history entry ---- */
        if ((unsigned char)ch == CTRL_P)
        {
            const char *entry;
            if (hist_pos == 0)
                strncpy(saved, linebuf, HIST_LEN - 1);
            entry = hist_get(hist_pos + 1);
            if (entry == NULL) continue;        /* already at oldest */
            hist_pos++;
            /* Load entry: erase line, replace with history entry */
            for (i = 0; i < cursor; i++) prtchr(8);
            for (i = 0; i < len;    i++) prtchr(' ');
            for (i = 0; i < len;    i++) prtchr(8);
            strncpy(linebuf, entry, HIST_LEN - 1);
            linebuf[HIST_LEN - 1] = '\0';
            len = cursor = (int)strlen(linebuf);
            for (i = 0; i < len; i++) prtchr(linebuf[i]);
            continue;
        }

        /* ---- Ctrl-N: next history entry / back to draft ---- */
        if ((unsigned char)ch == CTRL_N)
        {
            const char *entry;
            if (hist_pos == 0) continue;        /* nothing newer */
            hist_pos--;
            entry = (hist_pos == 0) ? saved : hist_get(hist_pos);
            if (entry == NULL) entry = "";
            /* Load entry: erase line, replace with history entry */
            for (i = 0; i < cursor; i++) prtchr(8);
            for (i = 0; i < len;    i++) prtchr(' ');
            for (i = 0; i < len;    i++) prtchr(8);
            strncpy(linebuf, entry, HIST_LEN - 1);
            linebuf[HIST_LEN - 1] = '\0';
            len = cursor = (int)strlen(linebuf);
            for (i = 0; i < len; i++) prtchr(linebuf[i]);
            continue;
        }

        /* ---- Ctrl-F: move cursor forward ---- */
        if ((unsigned char)ch == CTRL_F)
        {
            if (cursor < len) {
                prtchr(linebuf[cursor]);
                cursor++;
            }
            continue;
        }

        /* ---- Ctrl-B: move cursor backward ---- */
        if ((unsigned char)ch == CTRL_B)
        {
            if (cursor > 0) {
                prtchr(8);
                cursor--;
            }
            continue;
        }

        /* ---- Ctrl-A: move to start of line ---- */
        if ((unsigned char)ch == CTRL_A)
        {
            while (cursor > 0) { prtchr(8); cursor--; }
            continue;
        }

        /* ---- Ctrl-E: move to end of line ---- */
        if ((unsigned char)ch == CTRL_E)
        {
            while (cursor < len) { prtchr(linebuf[cursor]); cursor++; }
            continue;
        }

        /* ---- Ctrl-K: kill to end of line ---- */
        if ((unsigned char)ch == CTRL_K)
        {
            for (i = cursor; i < len; i++) prtchr(' ');
            for (i = cursor; i < len; i++) prtchr(8);
            len = cursor;
            linebuf[len] = '\0';
            continue;
        }

        /* ---- Backspace (^H = 8) or DEL / rubout (127) ---- */
        if ((unsigned char)ch == 8 || (unsigned char)ch == 127)
        {
            if (cursor > 0) {
                cursor--;
                len--;
                memmove(&linebuf[cursor], &linebuf[cursor+1], len - cursor + 1);
                prtchr(8);                              /* move screen cursor left  */
                for (i = cursor; i < len; i++) prtchr(linebuf[i]);
                prtchr(' ');                           /* erase the last character */
                for (i = cursor; i < len + 1; i++) prtchr(8); /* move back to cursor */
            }
            continue;
        }

        /* ---- Ctrl-R: emacs-style reverse incremental history search ----
         * Typed characters narrow a search query matched against history,
         * most-recent-first; repeating Ctrl-R jumps to the next older
         * match. Enter accepts and runs the match; Ctrl-G/Escape cancels
         * back to the line as it was; any other key ends the search and
         * leaves the match in the line for further editing. */
        if ((unsigned char)ch == CTRL_R)
        {
            char squery[HIST_LEN];
            char origbuf[HIST_LEN];
            int  orig_len, orig_cursor, orig_hist_pos;
            int  qlen, shown, off;
            const char *matched;
            char dispbuf[120];

            strncpy(origbuf, linebuf, HIST_LEN - 1);
            origbuf[HIST_LEN - 1] = '\0';
            orig_len = len; orig_cursor = cursor; orig_hist_pos = hist_pos;

            /* clear the currently displayed line */
            for (i = 0; i < cursor; i++) prtchr(8);
            for (i = 0; i < len;    i++) prtchr(' ');
            for (i = 0; i < len;    i++) prtchr(8);

            squery[0] = '\0';
            qlen   = 0;
            off    = 0;
            matched = NULL;
            shown  = 0;

            for (;;)
            {
                snprintf(dispbuf, sizeof(dispbuf), "(reverse-i-search)`%s': %s",
                         squery, matched ? matched : "");
                for (i = 0; i < shown; i++) prtchr(8);
                for (i = 0; i < shown; i++) prtchr(' ');
                for (i = 0; i < shown; i++) prtchr(8);
                prtmsg(dispbuf);
                shown = (int)strlen(dispbuf);

                ch = getchr();

                if (ch == CR || (unsigned char)ch == 13)
                {
                    for (i = 0; i < shown; i++) prtchr(8);
                    for (i = 0; i < shown; i++) prtchr(' ');
                    for (i = 0; i < shown; i++) prtchr(8);
                    if (matched != NULL)
                    {
                        strncpy(linebuf, matched, HIST_LEN - 1);
                        linebuf[HIST_LEN - 1] = '\0';
                    }
                    len = cursor = (int)strlen(linebuf);
                    prtmsg(linebuf);
                    prtchr('\n');
                    hist_push(linebuf);
                    memcpy(inbuf, linebuf, len);
                    inbuf[len]   = CR;
                    inbuf[len+1] = CR;
                    pos = 0;
                    return;
                }

                if ((unsigned char)ch == CTRL_G || ch == 0x1B)
                {
                    for (i = 0; i < shown; i++) prtchr(8);
                    for (i = 0; i < shown; i++) prtchr(' ');
                    for (i = 0; i < shown; i++) prtchr(8);
                    strncpy(linebuf, origbuf, HIST_LEN - 1);
                    linebuf[HIST_LEN - 1] = '\0';
                    len = orig_len; cursor = orig_cursor; hist_pos = orig_hist_pos;
                    prtmsg(linebuf);
                    break;
                }

                if ((unsigned char)ch == CTRL_R)
                {
                    if (squery[0] != '\0' && off != 0)
                    {
                        int next = hist_find(squery, off + 1);
                        if (next != 0) { off = next; matched = hist_get(off); }
                    }
                    continue;
                }

                if ((unsigned char)ch == 8 || (unsigned char)ch == 127)
                {
                    if (qlen > 0)
                    {
                        squery[--qlen] = '\0';
                        off = (qlen > 0) ? hist_find(squery, 1) : 0;
                        matched = (off != 0) ? hist_get(off) : NULL;
                    }
                    continue;
                }

                if ((unsigned char)ch >= 32 && qlen < HIST_LEN - 1)
                {
                    squery[qlen++] = ch;
                    squery[qlen] = '\0';
                    off = hist_find(squery, 1);
                    matched = (off != 0) ? hist_get(off) : NULL;
                    continue;
                }

                /* Any other key: end the search, keep the match (if any)
                   as the current line, and drop this keypress. */
                for (i = 0; i < shown; i++) prtchr(8);
                for (i = 0; i < shown; i++) prtchr(' ');
                for (i = 0; i < shown; i++) prtchr(8);
                if (matched != NULL)
                {
                    strncpy(linebuf, matched, HIST_LEN - 1);
                    linebuf[HIST_LEN - 1] = '\0';
                }
                len = cursor = (int)strlen(linebuf);
                prtmsg(linebuf);
                break;
            }
            continue;
        }

        /* ---- Normal printable character: insert at cursor ---- */
        if ((unsigned char)ch >= 32 && len < maxtkn)
        {
            memmove(&linebuf[cursor+1], &linebuf[cursor], len - cursor);
            linebuf[cursor] = ch;
            len++;
            for (i = cursor; i < len; i++) prtchr(linebuf[i]);
            cursor++;
            for (i = cursor; i < len; i++) prtchr(8);
        }
    }
}


/*
 *
 * Get one memory addr.
 *
 */


int32 getadr(void)

{

  int32 adr,i;

  ignblk();
  adr = i = 0;
  while((i = gethex()) != -1)
  {
    adr = adr * 16 + i;
  }
  return(adr);
}



int getbyt(void)

{

int byt,i;

  ignblk();
  byt = i = 0;
  byt = gethex();
  i = gethex();
  if ((i < 0) || (byt < 0))
    byt = -1;
  else
     byt = byt * 16 + i;
  return(byt);
}


int gethex(void)

{

int i;

  i = nxtch();
  if(i > 96)
     i -= 32;
  i -= 48;
  if (i > 9)
     i -= 7;
  if (i > 15)
     i = -1;
  if(i < 0)
     i = -1;
  return(i);
}

int nxtch(void)

{
  int i;
  i = inbuf[pos];
  if(i != CR)
     pos++;
  return(i);
}

void prtadr(int32 adr)
{
  if ((adr >= 0x10000) || (adr < 0))
  {
    prthex((adr >> 24) & 0xff);
    prthex((adr >> 16) & 0xff);
    adr = adr & 0xffff;
  }
  prthex(adr >> 8);
  prthex(adr & 0xff);
}


void prthex(byte hex)
{
  prtnib((hex >> 4) & 0x0F);
  prtnib(hex & 15);
}


void prtnib(byte nib)
{
  if(nib > 9)
    nib += 7;
  prtchr(nib + '0');
}



void lower(void )

{
  int i;

  for(i = 0; i < maxtkn; i++)
  if (inbuf[i] > 64)
      if (inbuf[i] < 91)
         inbuf[i] += 32;
}

/*
 *
 * Ignore blanks.
 *
 */


int ignblk(void )

{
  while(inbuf[pos] == 32)
    pos++;
  return(inbuf[pos]);
}

/*
 *
 * Search for command in an array.
 * Then execurte it. It works together with the struct in the beginning of
 * of this program.
 *
 */

int sekcom(void )

{

int16 i,j,miss,found;

  pos = 0;
  i = 0;
  found = 0;
  if(ignblk() != CR)
  {
     while((found == 0) && (i < MAXCOMMAND))
     {
       j = 0;
       do
       {
         miss = 0;
         if(inbuf[pos + j] == '.')
         {
            found = 1;
            j++;
         }
         if(commands[i].name[j] == 0)
           found=1;
         if(commands[i].name[j] == inbuf[pos + j])
           j++;
         else
           miss = 1;
       }
       while(miss + found == 0);
       i++;
     }
     if (found == 0)
        i = -1;
     else
        pos += j;
  }
return(i);
}

/*
 *
 * Command to list memory.
 *
 */


void do_list(void)

{

  int32 x,limit,width;
  int i;
  char ch;

  x = getadr();
  limit = getadr();
  width = getadr();
  if ((width == 0) || (width > 32))
     width = 8;

  while (1)
  {
    prtadr(x);
    prtmsg("  ");
    for(i = 0; i < width; i++)
    {
      prthex(memr(x + i));
      prtchr(32);
    }
    prtchr(32);
    prtchr(32);
    for(i = 0; i < width; i++)
    {
      ch = memr(x++);
      ch = ch & 0x7F;
      if(ch < 32)
        ch = 46;
      prtchr(ch);
    }
    prtchr('\n');
    if (limit == 0)
    {
      if (getchr() == CR)
         break;
    }
    else
    {
      if (x > limit)
         break;
    }
  }
}


/*
 *
 * List all available commands.
 *
 */

void do_info(void )

{
   int i,j,ch;

   for(i = 0; i < MAXCOMMAND; i++)
   {
      j = 0;
      do
      {
        ch = commands[i].name[j++];
        prtchr(ch);
      }
      while(ch != 0);
      prtchr('\n');
   }
}

/*
 *
 * command to modify memory.
 *
 * Subcommands are:
 *
 * + - increase memory location with one without modifying current.
 * - - decrease memory location with one without modifying current.
 * <CR> read the same byte once again.
 * . - will break programming.
 * j - will calcualte data as offset for short branch
 * " - (open, only first appearance is significant) is used to enter ASCII
 *     data
 *
 */

void do_prog(void )

{
  int32 x;
  char ch;
  int i,bryt;

  x = getadr();
  bryt = 0;
  while(bryt == 0)
  {
    prtadr(x);
    prtchr(32);
    prthex(memr(x));
    prtchr(32);
    buffer();
    ch = inbuf[0];
    switch(ch)
    {
      case '+' : x++;
                 break;
      case '-' : x--;
                 break;
      case 'J' :
      case 'j' : memw(x, (getbyt() - x + 2) & 0xff);
                 break;
      case '"' : for(i = 1; inbuf[i] != CR;)
                     memw(x++, inbuf[i++]);
                 break;
      case CR  : break;
      case 46  : bryt = 1;
                 break;
      default  : while((i = getbyt()) != -1)
                     memw(x++, i);
                 break;
    }
  }
}

/*
 *
 * Move (copy) memory command. Syntax move <start> <end> <to>
 *
 */

void do_move(void )

{

  int32 start,stop,till;

  start = getadr();
  stop = getadr();
  till = getadr();

  while(start != stop)
    memw(till++, memr(start++));
}

void do_exit(void )

{
}

/*
 *
 * Fill memory with a one byte pattern. fill <start> <stop+1> <byte>
 *
 */

void do_fill(void )

{

  int32 start,stop;
  byte data;

  start = getadr();
  stop = getadr();
  data = getbyt();
  while(start != stop)
    memw(start++, data);
}

/*
 *
 * Calculate checksum of memory: check <start> <end> sums each byte
 * from start up to (but not including) end - e.g. check 1000 1200
 * covers $1000-$11FF - as a plain unsigned 8-bit value into a 16-bit
 * result that wraps on overflow, printed as 4 hex digits.
 *
 * memr() returns a plain (signed) char, so a byte >= 0x80 sign-extends
 * to a negative value on the usual arithmetic conversion to int -
 * masking with 0xFF here recovers the original 0-255 byte value
 * before adding it in. sum itself is unsigned so it can never go
 * negative either, which matters because prtadr() prints extra
 * (garbage, from its point of view) leading hex digits for anything
 * negative or over 16 bits - unsigned wraparound keeps the result a
 * clean 4-digit hex value no matter how much the sum overflows.
 *
 */

void do_check(void )

{

  int32 start,stop;
  unint16 sum;

  start = getadr();
  stop = getadr();
  sum = 0;
  while (start<stop)
    sum += memr(start++) & 0xFF;
  prtmsg("Checksum ");
  prtadr(sum);
  prtmsg("\n");
}

/*
 *
 * Verify 2 areas of memory. Syntax as in move command.
 *
 */

void do_verf(void)

{

  int32 start,stop,mot;

  start = getadr();
  stop = getadr();
  mot = getadr();
  while (start<stop)
  {
    if(memr(start) != memr(mot))
    {
       prtmsg("Error at ");
       prtadr(start);
       prtchr(32);
       prthex(memr(start));
       prtchr(32);
       prthex(memr(mot));
       prtmsg("\n");
       if (getchr() == CR)
         break;
    }
    start++;
    mot++;
  }
}

/*
 *
 * Command to locate a specific string in memory.
 *
 * The string to search for is stored at a certain memory address,
 * with a one byte count byte at first.
 * to search for the data one specifies:
 *
 * find <matching string in memory with countbyte> <start search address>
 *
 */

void do_find(void )

{
  int32 pattern,leta,i,lengd;

  pattern = getadr();
  leta = getadr();
  lengd = memr(pattern++);
  do
  {
    i = 0;
    while((memr(pattern + i) == memr(leta + i)) & (i < lengd))
       i++;
    if(i >= lengd)
    {
      prtadr(leta);
      leta++;
      prtchr('\n');
      if ((i = getchr()) == CR)
         leta = 0;
    }
    else
      leta++;
  }
  while(leta != 0);
}

/*
 *
 * Read and store one byte at a given adress.
 *
 */

void do_modify(void )

{
  int adr,data;

  adr = getadr();
  data = getbyt();
  memw(adr,data);
}

/*
 *
 * Print register contents (used for the simulator)
 *
 */

void do_reg(void )
{
    regs();
    prtmsg("\n");
}

void regs(void )
{
    ccadj();
    printf("PC=%04X A=%02X B=%02X X=%04X ",pc,accu[1],accu[0],ix[0]);
    printf("Y=%04X U=%04X S=%04X DP=%02X CC=%02X E=%02X F=%02X",ix[1],ix[2],ix[3],accu[3],accu[2],acce,accf);
    fflush(stdout);
}          

/*
 *
 * Set the 6809 simulator register to a certain value.
 * Note that all registers should be named by one character only !
 * "pc" and "cc" are also accepted as two-character aliases for "p"
 * and "c" - without this, a trailing 'c' would otherwise be consumed
 * by getadr()/getbyt() as a valid hex digit (e.g. "set pc 2000" would
 * silently set PC to $0C instead of $2000).
 *
 */

void do_set(void )

{
    ignblk();
    switch(nxtch())
    {
        case 'a' :      accu[1] = getbyt();
                        break;
        case 'b' :      accu[0] = getbyt();
                        break;
        case 'c' :      if (inbuf[pos] == 'c')
                            pos++;
                        accu[2] = getbyt();   /* accu[2] = cc (see sim09.c) */
                        break;
        case 'd' :      accu[1] = getbyt();
                        accu[0] = getbyt();
                        break;
        case 'e' :      acce = getbyt();
                        break;
        case 'f' :      accf = getbyt();
                        break;
        case 'w' :      acce = getbyt();  /* 6309 W = E:F, see sim09.c */
                        accf = getbyt();
                        break;
        case 'x' :      ix[0] = getadr();
                        break;
        case 'y' :      ix[1] = getadr();
                        break;
        case 'u' :      ix[2] = getadr();
                        break;
        case 's' :      ix[3] = getadr();
                        break;
        case 'p' :      if (inbuf[pos] == 'c')
                            pos++;
                        pc = getadr();
                        break;
        default  :      prtmsg("unknown register\n");
    }
    do_reg();
}

/*
 *
 * Select which mode the simulator shall work in.
 *
 */

void do_mode(void )

{
    prtmsg("Available switches: p <addr>,q <addr>,s,t,i,r,g <addr>,h (help)\n");
    do
    {
        ignblk();
        switch(nxtch())
        {
                case 't':  mode_tr = not(mode_tr);
                           break;
                case 's':  mode_s = not(mode_s);
                           break;
                case 'p':  mode_p = not(mode_p);
                           ignblk();
                           break_pc = getadr();
                           if (break_pc != 0)
                                mode_p = 1;
                           break;
                case 'q':  mode_q = not(mode_q);
                           ignblk();
                           break_qpc = getadr();
                           if (break_qpc != 0)
                                mode_q = 1;
                           break;
                case 'g':  mode_g = not(mode_g);
                           ignblk();
                           break_g = getadr();
                           if (break_g != 0)
                                mode_g = 1;
                           break;
                case 'l':  mode_sim = not(mode_sim);
                           break;
                case 'r':  mode_r = not(mode_r);
                           break;
                case 'i':  mode_i = not(mode_i);
                           break;
                case 'h':  printf("p = break on PC value\n");
                           prtmsg("q = break on PC value\n");
                           prtmsg("g = break after a number of instr\n");
                           prtmsg("s = single step mode on/off\n");
                           prtmsg("t = trace mode on/off\n");
                           prtmsg("i = print instructions\n");
                           prtmsg("r = print registers\n");
                           prtmsg("l = output to fit sim program\n");
                           prtmsg("To enable option use 'mode <options>'\n");
                case CR :  break;
        }
    }
    while(ignblk() != CR);
    if (mode_p != 0)
        printf ("Break on PC = $%04x\r\n",break_pc);
    if (mode_q != 0)
        printf ("Break on PC = $%04x\r\n",break_qpc);
    if (mode_g != 0)
        printf ("Number of lines to go: $%04X\r\n",break_g);
    if (mode_s != 0)
        printf ("Single step is on\r\n");
    if (mode_tr != 0)
        printf ("Trace mode is on\r\n");
    if (mode_i != 0)
        printf ("Instruction display is on\r\n");
    if (mode_r != 0)
        printf ("Registers printed at single step\r\n");
    if (mode_sim != 0)
        printf ("Assembler will output in sim form\r\n");
}

int not(int n)
{
    if (n == 0)
        return(1);
    else
        return(0);
}

/*
 *
 * Load a file into memory.
 *
 */

void do_load(void )

{

  int32 ch,adr;

  adr = getadr();
  ignblk();
  scancr();
  fil = fopen((char *)&inbuf[pos],"rb");
  if (fil == NULL)
    prtmsg("The file does not exist\n");
  else
  {
    while((ch = fgetc(fil)) != EOF)
      memw(adr++,ch);
    prtmsg("End address : ");
    prtadr(adr);
    prtmsg("\n");
  }
}

/*
 *
 * Load a Motorola S1 record file into memory.
 *
 * S1 record format:
 *   S1 <len> <addr-hi> <addr-lo> <data bytes...> <checksum>
 *
 * <len> is the number of remaining bytes in the record (addr + data + checksum).
 * The checksum byte is the one's complement of the sum of all bytes
 * from <len> onwards (not verified here, just skipped).
 *
 * Syntax: s1load <filename>
 *
 */

void do_s1load(void)
{
  int32 adr;
  int32 block_start;
  unsigned int len, hi, lo, byte_val, i;
  int   records = 0, errors = 0;
  char  rectype;
  char  namebuf[32];
  int   namelen;

  ignblk();
  scancr();
  fil = fopen((char *)&inbuf[pos], "r");
  if (fil == NULL)
  {
    prtmsg("The file does not exist\n");
    return;
  }

  adr = 0;

  while (fscanf(fil, " S%c", &rectype) == 1)
  {
    switch (rectype)
    {
      case '0': /* S0 header record - extract and print the name */
        if (fscanf(fil, "%02x%02x%02x", &len, &hi, &lo) != 3)
        {
          fscanf(fil, "%*[^\n]\n");
          break;
        }
        /* len = addr(2) + name bytes + checksum(1), addr is always 0000 */
        namelen = (int)len - 3;
        if (namelen < 0) namelen = 0;
        /* Read name bytes as hex pairs; store up to namebuf size - 1 */
        for (i = 0; i < (unsigned int)namelen; i++)
        {
          if (fscanf(fil, "%02x", &byte_val) != 1) break;
          if ((int)i < (int)sizeof(namebuf) - 1)
            namebuf[i] = (char)byte_val;
          /* bytes beyond namebuf size are read and discarded - must consume
             them all so the file position is correct for the next record */
        }
        namebuf[(namelen < (int)sizeof(namebuf)) ? namelen : (int)sizeof(namebuf)-1] = '\0';
        fscanf(fil, "%*02x"); /* skip checksum */
        if (namelen > 0)
        {
          prtmsg("Name: ");
          prtmsg(namebuf);
          prtmsg("\n");
        }
        break;

      case '1': /* S1 data record with 2-byte address */
        if (fscanf(fil, "%02x", &len) != 1) { errors++; break; }
        if (fscanf(fil, "%02x%02x", &hi, &lo) != 2) { errors++; break; }
        adr = (int32)(hi * 0x100 + lo);
        block_start = adr;
        len -= 3; /* subtract 2 addr bytes + 1 checksum byte */
        for (i = 0; i < len; i++)
        {
          if (fscanf(fil, "%02x", &byte_val) != 1) { errors++; break; }
          memw(adr++, byte_val);
        }
        fscanf(fil, "%*02x"); /* skip checksum */
        /* Print block address using prtmsg/prtadr (avoids stdio/write sync issues) */
        prtmsg("  $");
        prtadr(block_start);
        prtmsg(" - $");
        prtadr(adr - 1);
        prtmsg("\n");
        records++;
        break;

      case '9': /* end record - we're done */
        goto done;

      default: /* unknown record type - skip line */
        fscanf(fil, "%*[^\n]\n");
        break;
    }
  }

done:
  fclose(fil);
  if (errors == 0)
  {
    prtmsg("Loaded ");
    /* print record count as decimal */
    {
      int32 n = (int32)records;
      if (n >= 1000) prtchr((char)('0' + n/1000));
      if (n >= 100)  prtchr((char)('0' + (n/100)%10));
      if (n >= 10)   prtchr((char)('0' + (n/10)%10));
      prtchr((char)('0' + n%10));
    }
    prtmsg(" records, last addr: $");
    prtadr(adr);
    prtmsg("\n");
  }
  else
  {
    printf("%d records loaded, %d errors\r\n", records, errors);
  }
}

/*
 *
 * Send memory as a Motorola S1 record file to stdout.
 *
 * Syntax: s1send <start> <end> [name]
 *
 * Emits an S0 header record (using name if given, else "MGMON"),
 * then S1 data records of up to 16 bytes each, then an S9 end record.
 *
 * S1 record format:
 *   S1 <len> <addr-hi> <addr-lo> <data...> <checksum>
 * where len = number of remaining bytes (addr + data + checksum),
 * and checksum = one's complement of the sum of all bytes from len onward.
 *
 */

#define S1_RECLEN  16   /* data bytes per S1 record */

/*
 * Write one character, and if delay_us is nonzero (s1port, talking to
 * real hardware that can't necessarily keep up at full speed), pace
 * the next one by that many microseconds. When pacing, bypass stdio's
 * buffering entirely and issue a raw write() straight to the
 * underlying fd - matching, byte for byte, the raw write() calls a
 * from-scratch Python termios test used when it worked reliably
 * against the real board, so there's no buffering-layer difference
 * left to account for. delay_us is 0 for s1send's stdout output,
 * where no pacing is wanted and plain buffered fputc() is fine.
 *
 * write() only queues the byte in the kernel's tty output buffer - it
 * returns as soon as the byte is accepted, not once it has actually
 * gone out over the wire. A USB-serial adapter's driver is free to
 * hold several queued bytes and send them as one USB transfer, which
 * would silently turn our per-character pacing into bursts by the
 * time they reach the board, no matter how long we usleep() between
 * write() calls. tcdrain() blocks until the kernel confirms the
 * output has actually been transmitted, forcing one real transfer per
 * byte before we sleep and queue the next - so the delay is between
 * actual transmissions, not just between write() syscalls.
 */
static void s1_putc(FILE *out, int delay_us, int ch)
{
    if (delay_us > 0)
    {
        unsigned char c = (unsigned char)ch;
        write(fileno(out), &c, 1);
        tcdrain(fileno(out));
        usleep((useconds_t)delay_us);
    }
    else
    {
        fputc(ch, out);
    }
}

static void s1_puts(FILE *out, int delay_us, const char *s)
{
    while (*s)
        s1_putc(out, delay_us, (unsigned char)*s++);
}

static void s1_puthex(FILE *out, int delay_us, int val)  /* one byte as two hex digits */
{
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", val & 0xFF);
    s1_puts(out, delay_us, buf);
}

/*
 * Emit start..stop as S1 records to any open stream - stdout for
 * s1send, or a serial port's FILE* for s1port (with delay_us pacing
 * each byte). If framed is nonzero, wraps them in an S0 header
 * (carrying name) and a closing S9, the standard Motorola-file
 * convention s1send uses. If framed is zero, sends only the bare S1
 * line(s) with nothing else - what s1port uses, since the UK board's
 * s1load only understands S1 records and gets stuck on S0/S9.
 */
static void s1_emit(FILE *out, int delay_us, int framed, int32 start, int32 stop, char *name, int namelen)
{
  int32  addr_s1, i;
  int    len, sum, datalen, ch;

  if (framed)
  {
    /* S0 header record: len = namelen + 3 (2 addr bytes + 1 checksum) */
    len = namelen + 3;
    sum = len + 0 + 0;   /* addr bytes are 0x00 0x00 for S0 */
    s1_puts(out, delay_us, "S0");
    s1_puthex(out, delay_us, len);
    s1_puts(out, delay_us, "0000");
    for (i = 0; i < namelen; i++)
    {
      s1_puthex(out, delay_us, name[i]);
      sum += name[i] & 0xFF;
    }
    s1_puthex(out, delay_us, ~sum);
    s1_puts(out, delay_us, "\n");
    fflush(out);
  }

  /* S1 data records */
  addr_s1 = start;
  while (addr_s1 < stop)
  {
    datalen = stop - addr_s1;
    if (datalen > S1_RECLEN)
      datalen = S1_RECLEN;

    len = datalen + 3;   /* 2 addr bytes + data + 1 checksum */
    sum = len + ((addr_s1 >> 8) & 0xFF) + (addr_s1 & 0xFF);

    s1_puts(out, delay_us, "S1");
    s1_puthex(out, delay_us, len);
    s1_puthex(out, delay_us, addr_s1 >> 8);
    s1_puthex(out, delay_us, addr_s1);

    for (i = 0; i < datalen; i++)
    {
      ch = memr(addr_s1 + i) & 0xFF;
      s1_puthex(out, delay_us, ch);
      sum += ch;
    }
    s1_puthex(out, delay_us, ~sum);
    s1_puts(out, delay_us, "\n");
    fflush(out);

    addr_s1 += datalen;
  }

  if (framed)
  {
    /* S9 end record: addr field = start address, len = 3 */
    len = 3;
    sum = len + ((start >> 8) & 0xFF) + (start & 0xFF);
    s1_puts(out, delay_us, "S9");
    s1_puthex(out, delay_us, len);
    s1_puthex(out, delay_us, start >> 8);
    s1_puthex(out, delay_us, start);
    s1_puthex(out, delay_us, ~sum);
    s1_puts(out, delay_us, "\n");
    fflush(out);
  }
}

void do_s1send(void)
{
  int32  start, stop;
  char   name[32];
  int    namelen, ch;

  start = getadr();
  stop  = getadr();

  /* Collect optional name from rest of input line */
  ignblk();
  namelen = 0;
  while ((ch = nxtch()) != CR && namelen < (int)(sizeof(name) - 1))
    name[namelen++] = ch;
  name[namelen] = 0;
  if (namelen == 0)
  {
    name[0] = 'M'; name[1] = 'G'; name[2] = 'M';
    name[3] = 'O'; name[4] = 'N'; name[5] = 0;
    namelen = 5;
  }

  s1_emit(stdout, 0, 1, start, stop, name, namelen);
}

/*
 *
 * Save memory as a complete Motorola S-record file (S0/S1/S9), same
 * format as s1send, but to a file on this machine instead of stdout.
 *
 * Syntax: s1save <start> <end> <file>
 *
 */

void do_s1save(void)
{
  int32 start, stop;
  FILE  *f;

  start = getadr();
  stop  = getadr();

  if (ignblk() == CR)
  {
    prtmsg("Usage: s1save <start> <end> <file>\n");
    return;
  }
  scancr();

  f = fopen((char *)&inbuf[pos], "w");
  if (f == NULL)
  {
    prtmsg("Could not create file\n");
    return;
  }

  s1_emit(f, 0, 1, start, stop, "MGMON", 5);
  fclose(f);
}

/*
 *
 * Save a file from memory.
 *
 */

void do_save(void )

{

  int32 start,stop;

  start = getadr();
  stop = getadr();
  if(ignblk() == CR)
    prtmsg("Syntax error");
  else
  {
    scancr();
    fil = fopen((char *)&inbuf[pos],"wb");
    while(start != stop)
      fputc(memr(start++),fil);
    fclose(fil);
  }

}

#define TERM_MAXDEV  16

/*
 *
 * Scan /dev for USB-serial device nodes (cu.usbserial-* and
 * cu.usbmodem*, the two prefixes macOS uses for FTDI/CH34x/CP210x-
 * style adapters). Returns the count found, filling names[] with up
 * to TERM_MAXDEV of them.
 *
 */

static int term_scan_devices(char names[TERM_MAXDEV][64])
{
    DIR *dp;
    struct dirent *de;
    int count;

    count = 0;
    dp = opendir("/dev");
    if (dp == NULL)
        return(0);
    while (count < TERM_MAXDEV && (de = readdir(dp)) != NULL)
    {
        if (strncmp(de->d_name, "cu.usbserial", 12) == 0 ||
            strncmp(de->d_name, "cu.usbmodem", 11) == 0)
        {
            strncpy(names[count], de->d_name, sizeof(names[0]) - 1);
            names[count][sizeof(names[0]) - 1] = '\0';
            count++;
        }
    }
    closedir(dp);
    return(count);
}

/*
 * Parse an optional trailing baud-rate token from the current input
 * line, used by term, s1port and sendfile. Matched as an exact decimal
 * string rather than read via getadr(), since this monitor's numbers
 * are otherwise all hex - "9600" would silently mean 0x9600 (38400)
 * baud if parsed as hex. Leaves *baud/baudtok at the last rate actually
 * used (by any of term/s1port/sendfile) if no token is present, so e.g.
 * "term 2400" followed by a bare "sendfile foo" sends at 2400 rather
 * than silently reverting to 9600. That last-used rate starts out as
 * 9600 before any of the three commands has run. Returns 0 on success,
 * -1 if a token was present but didn't match any of the supported
 * rates (last-used is left unchanged in that case).
 *
 * *baud is a plain integer baud rate (9600, 19200, ...), not
 * necessarily one of termios's named B-constants - term_open_device()
 * sets the real rate via the IOSSIOSPEED ioctl, which (on macOS)
 * accepts any integer the underlying driver supports, not just the
 * fixed B-table. That's what lets 153600 work even though this SDK's
 * <sys/termios.h> has no B153600 macro.
 *
 */

static speed_t term_last_baud = 9600;
static char term_last_baudtok[8] = "9600";

static int term_parse_baud(speed_t *baud, char *baudtok, size_t baudtok_sz)
{
    int j, k;
    speed_t new_baud;

    strncpy(baudtok, term_last_baudtok, baudtok_sz - 1);
    baudtok[baudtok_sz - 1] = '\0';
    *baud = term_last_baud;

    ignblk();
    scancr();
    if (inbuf[pos] == 0)
        return(0);

    j = pos;
    k = 0;
    while (inbuf[j] != 0 && inbuf[j] != ' ' && k < (int)baudtok_sz - 1)
        baudtok[k++] = inbuf[j++];
    baudtok[k] = '\0';

    if (strcmp(baudtok, "2400") == 0)
        new_baud = 2400;
    else if (strcmp(baudtok, "9600") == 0)
        new_baud = 9600;
    else if (strcmp(baudtok, "19200") == 0)
        new_baud = 19200;
    else if (strcmp(baudtok, "28800") == 0)
        new_baud = 28800;
    else if (strcmp(baudtok, "38400") == 0)
        new_baud = 38400;
    else if (strcmp(baudtok, "76800") == 0)
        new_baud = 76800;
    else if (strcmp(baudtok, "153600") == 0)
        new_baud = 153600;
    else
        return(-1);

    *baud = new_baud;
    pos = j;

    term_last_baud = new_baud;
    strncpy(term_last_baudtok, baudtok, sizeof(term_last_baudtok) - 1);
    term_last_baudtok[sizeof(term_last_baudtok) - 1] = '\0';

    return(0);
}

/*
 *
 * Scan /dev for USB-serial devices (see term_scan_devices), let the
 * user pick one if there's more than one match, open it raw at the
 * given baud rate, and report the path chosen via path_out. Used by
 * both term and s1port. Returns an open fd, or -1 (after printing a
 * suitable message) if none was available, chosen, or openable.
 *
 */

static int term_open_device(speed_t baud, char *path_out, size_t path_out_sz)
{
    char  names[TERM_MAXDEV][64];
    char  line[100];
    int   count, choice, i;
    int   fd;
    struct termios tio;

    count = term_scan_devices(names);
    if (count == 0)
    {
        prtmsg("No USB serial devices found in /dev.\n");
        return(-1);
    }

    if (count == 1)
    {
        choice = 0;
    }
    else
    {
        prtmsg("USB serial devices found:\n");
        for (i = 0; i < count; i++)
        {
            snprintf(line, sizeof(line), "  %d) %s\n", i + 1, names[i]);
            prtmsg(line);
        }
        prtmsg("Select device number: ");
        buffer();
        choice = (int)getadr() - 1;
        if (choice < 0 || choice >= count)
        {
            prtmsg("Cancelled.\n");
            return(-1);
        }
    }

    snprintf(path_out, path_out_sz, "/dev/%s", names[choice]);

    fd = open(path_out, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        snprintf(line, sizeof(line), "Could not open %s\n", path_out);
        prtmsg(line);
        return(-1);
    }

    if (tcgetattr(fd, &tio) == 0)
    {
        /* Build every flag word from scratch instead of layering
           cfmakeraw() on top of whatever tcgetattr() returned.
           cfmakeraw() only clears specific bits (CSIZE/PARENB in
           c_cflag, a handful in c_iflag/c_lflag) - any OTHER bit this
           port's driver happens to report (stale CSTOPB meaning 2
           stop bits, IGNBRK, etc. left over from its previous state)
           passes straight through untouched. That's invisible to a
           loopback/pty test, which doesn't enforce real UART framing,
           but can silently corrupt every byte against a real receiver.
           This is exactly the 8N1/raw config verified working against
           the real board via a from-scratch Python termios setup. */
        tio.c_iflag = 0;
        tio.c_oflag = 0;
        tio.c_cflag = CS8 | CLOCAL | CREAD;
        tio.c_lflag = 0;
        cfsetspeed(&tio, B9600);  /* placeholder - IOSSIOSPEED overrides below */
        tio.c_cc[VMIN]  = 1;
        tio.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &tio);

        /*
         * Set the actual rate via IOSSIOSPEED rather than relying on
         * cfsetspeed()/tcsetattr() alone: those only guarantee the
         * fixed termios B-table, but macOS's IOSSIOSPEED ioctl takes
         * any integer baud rate the underlying driver supports. That's
         * what lets 153600 work even though there's no B153600 macro -
         * see the note above term_parse_baud().
         */
        ioctl(fd, IOSSIOSPEED, &baud);
    }

    return(fd);
}

/*
 * ADM3A terminal emulation for do_term()'s incoming (board -> Mac)
 * byte stream. MGMon's own terminal driver speaks a handful of Lear
 * Siegler ADM-3A control codes rather than ANSI/VT100, so without
 * this a real ANSI terminal (Terminal.app, iTerm, ...) would just
 * print raw control characters instead of clearing the screen or
 * positioning the cursor. This translates the ones MGMon uses into
 * the equivalent ANSI/VT100 escape sequence:
 *
 *   0x0C (^L)         - clear screen + home        -> ESC [ H ESC [ 2 J
 *   0x12 (DC2)        - clear screen + home        -> ESC [ H ESC [ 2 J
 *   0x1A (^Z)         - clear screen + home        -> ESC [ H ESC [ 2 J
 *   0x18 (^X)         - clear to end of line        -> ESC [ K
 *   ESC = <row> <col> - cursor address, each of row -> ESC [ <row+1> ; <col+1> H
 *                       and col sent as the value+32
 *                       (0-based, so row 0 = 0x20)
 *   ESC E             - insert line                 -> ESC [ L
 *   ESC R             - delete line                 -> ESC [ M
 *   0x0B (^K)         - cursor up                   -> ESC [ A
 *
 * 0x08 (^H, cursor left) and 0x0A (^J, cursor down) are passed
 * through unchanged - every ANSI terminal already treats plain
 * backspace/linefeed as move-left/move-down when the local tty is in
 * raw mode (see term_raw()), same as ADM-3A does, so no translation
 * is needed for those two.
 *
 * Both 0x0C and 0x12 are mapped to clear-screen: confirmed against
 * the real board that ^L (0x0C) is what MGMon actually sends (the
 * real Lear Siegler ADM-3A also uses ^L for clear screen, not ^Z as
 * some references claim). 0x12 is kept mapped too in case anything
 * else in MGMon still emits it - harmless if it never actually
 * occurs. ^L is no longer cursor-right (that guess was wrong).
 *
 * 0x1A, 0x18, ESC E and ESC R are for the older mg-edit CRT editor
 * (a separate FLEX-era program, not MGMon), which targets a terminal
 * with hardware clear-to-EOL and hardware insert/delete line rather
 * than MGMon's conventions - added so that editor's redraw works
 * too, since both share this one translator (see below).
 *
 * This is a small state machine (state persists across calls so a
 * multi-byte sequence split across two read()s from the serial port
 * still gets parsed correctly) rather than a single function, since
 * "ESC = row col" is 4 bytes that can arrive in separate chunks.
 *
 * Not static: sim09.c's do_run() also calls this, for the "prt"
 * pseudo-instruction (case 32) that a simulated 6809 program uses to
 * print a character - the same ADM3A codes apply whether the bytes
 * come from the real board over serial or from a program running
 * under the simulator, so both share this one translator rather than
 * duplicating the state machine. See ADM3A_NORMAL etc. in mgmon.h.
 */
void adm3a_putc(int *state, int *row, unsigned char c)
{
    char seq[16];
    int  len, col;

    switch (*state)
    {
    case ADM3A_NORMAL:
        if (c == 0x0C || c == 0x12 || c == 0x1A)   /* ^L / DC2 / ^Z clear screen */
            write(STDOUT_FILENO, "\x1B[2J\x1B[H", 7);
        else if (c == 0x18)                       /* ^X clear to end of line */
            write(STDOUT_FILENO, "\x1B[K", 3);
        else if (c == 0x1B)
            *state = ADM3A_ESC;
        else if (c == 0x0B)                       /* ^K cursor up */
            write(STDOUT_FILENO, "\x1B[A", 3);
        else
            write(STDOUT_FILENO, &c, 1);
        break;

    case ADM3A_ESC:
        if (c == '=')
            *state = ADM3A_ESC_EQ;
        else if (c == 'E')                        /* insert line */
            write(STDOUT_FILENO, "\x1B[L", 3);
        else if (c == 'R')                        /* delete line */
            write(STDOUT_FILENO, "\x1B[M", 3);
        else
        {
            /* Not a sequence we know - pass both bytes through
               unchanged rather than swallowing the ESC. */
            write(STDOUT_FILENO, "\x1B", 1);
            write(STDOUT_FILENO, &c, 1);
        }
        if (c != '=')
            *state = ADM3A_NORMAL;
        break;

    case ADM3A_ESC_EQ:
        *row = ((int)c - 32) & 0xFF;
        *state = ADM3A_ESC_EQ_ROW;
        break;

    case ADM3A_ESC_EQ_ROW:
        col = ((int)c - 32) & 0xFF;
        len = snprintf(seq, sizeof(seq), "\x1B[%d;%dH", *row + 1, col + 1);
        write(STDOUT_FILENO, seq, len);
        *state = ADM3A_NORMAL;
        break;
    }
}

/*
 *
 * Basic serial terminal with ADM3A emulation (see adm3a_putc above).
 * Relays keystrokes to a USB-serial device (FTDI, CH34x, CP210x, ...)
 * unchanged, and translates the small set of ADM3A control codes
 * MGMon sends back into ANSI/VT100 so screen-clear and cursor moves
 * display correctly. Press ESC to disconnect and return here.
 *
 * term_raw() deliberately leaves ICRNL on (see its comment) so Enter
 * reads as CR==10 for the command parser - but that also means every
 * 0x0D read from stdin, including a literal Ctrl-M, arrives here
 * already rewritten to 0x0A, indistinguishable from Ctrl-J. That's
 * wrong for a terminal that's supposed to relay raw keystrokes to the
 * board, so ICRNL is turned off on entry for the duration of this
 * function and restored on every exit path.
 *
 * Ctrl-L (0x0C) is relayed to the board like any other key, but is
 * also applied to the local screen immediately rather than waiting
 * for the board to echo it back - MGMon on a real board doesn't echo
 * keystrokes, so without this local echo, Ctrl-L would appear to do
 * nothing at all.
 *
 * 8N1, 9600 baud by default; 2400/19200/28800/38400/76800/153600 may be
 * given instead.
 *
 * An optional trailing hex byte picks the break (disconnect) character
 * in place of the default ESC ($1B), e.g. "term 03" stays at 9600 baud
 * but disconnects on Ctrl-C instead; "term 19200 03" sets both. Read
 * with getbyt() (two hex digits), matching this monitor's usual hex
 * byte convention (modify/fill/...) rather than baud's decimal one.
 *
 * No device argument: the whole input line is lowercased before a
 * command runs (see lower()), which would mangle a device path typed
 * directly - macOS names USB-serial nodes after the adapter's serial
 * number, which is often mixed case, and /dev is case-sensitive. So
 * instead this scans /dev itself and lets you pick from a list,
 * sidestepping the case problem entirely.
 *
 * Syntax: term [2400|9600|19200|28800|38400|76800|153600] [break-byte-hex]
 *
 */

void do_term(void)
{
    char  path[80];
    char  line[100];
    char  baudtok[8];
    speed_t baud;
    int   fd;
    fd_set rfds;
    int   maxfd;
    unsigned char ch;
    unsigned char buf[256];
    ssize_t n;
    int   adm3a_state;
    int   adm3a_row;
    struct termios term_before, term_noicrnl;
    int   have_tty;
    unsigned char breakchar;
    int   bc;

    breakchar = 0x1B;

    if (term_parse_baud(&baud, baudtok, sizeof(baudtok)) != 0)
    {
        /* Not a recognised baud token - try it as the break-byte
           instead, e.g. "term 03" (default baud, break on Ctrl-C). */
        bc = getbyt();
        if (bc < 0)
        {
            prtmsg("Usage: term [2400|9600|19200|28800|38400|76800|153600] [break-byte-hex]\n");
            return;
        }
        breakchar = (unsigned char)bc;
    }
    else
    {
        ignblk();
        scancr();
        if (inbuf[pos] != 0)
        {
            bc = getbyt();
            if (bc < 0)
            {
                prtmsg("Usage: term [2400|9600|19200|28800|38400|76800|153600] [break-byte-hex]\n");
                return;
            }
            breakchar = (unsigned char)bc;
        }
    }

    fd = term_open_device(baud, path, sizeof(path));
    if (fd < 0)
        return;

    snprintf(line, sizeof(line), "Connected to %s at %s baud. Press %02X to return.\n",
             path, baudtok, breakchar);
    prtmsg(line);

    have_tty = isatty(STDIN_FILENO);
    if (have_tty)
    {
        tcgetattr(STDIN_FILENO, &term_before);
        term_noicrnl = term_before;
        term_noicrnl.c_iflag &= ~ICRNL;
        tcsetattr(STDIN_FILENO, TCSANOW, &term_noicrnl);
    }

    adm3a_state = ADM3A_NORMAL;
    adm3a_row   = 0;

    maxfd = (fd > STDIN_FILENO) ? fd : STDIN_FILENO;
    for (;;)
    {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(fd, &rfds);

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0)
            break;

        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            if (read(STDIN_FILENO, &ch, 1) != 1)
                break;
            if (ch == breakchar)
                break;
            if (ch == 0x0C)                        /* ^L: clear locally too - */
                write(STDOUT_FILENO, "\x1B[2J\x1B[H", 7);  /* the board doesn't echo */
            write(fd, &ch, 1);
        }

        if (FD_ISSET(fd, &rfds))
        {
            int i;

            n = read(fd, buf, sizeof(buf));
            if (n <= 0)
            {
                prtmsg("\nSerial port closed.\n");
                break;
            }
            for (i = 0; i < (int)n; i++)
                adm3a_putc(&adm3a_state, &adm3a_row, buf[i]);
        }
    }

    if (have_tty)
        tcsetattr(STDIN_FILENO, TCSANOW, &term_before);

    close(fd);
    prtmsg("\nDisconnected.\n");
}

#define S1PORT_CHAR_DELAY_US  1000   /* 1ms between characters, for slow
                                        receivers/USB-serial links that
                                        can't keep up at full speed -
                                        shared by s1port and sendfile */

/*
 *
 * Send memory as bare S1 records (no S0 header, no S9 trailer) to a
 * USB-serial device, picked the same way term does (see
 * term_open_device). Unlike s1send, which writes a complete Motorola
 * S-record file (S0/S1/S9) for host tools, this sends only the S1
 * data line(s), matching the real 6809 board's (UK board running
 * MGMon) s1load receiver, which only understands 'S' + '1' + hex
 * digits - an S0 record is silently skipped (its bytes never contain
 * an 'S', so the receiver's hunt-for-next-record loop just scans
 * through it) and this code never emits S9. Each character is paced
 * (see S1PORT_CHAR_DELAY_US) so a simple receiver without flow control
 * has time to keep up.
 *
 * Per the board's own source (s1.s09), after the last data byte of an
 * S1 record it does NOT read/verify the trailing checksum and does
 * NOT return to the monitor - it just loops back to silently wait for
 * another 'S' record. The only way back to the monitor prompt is an
 * explicit 'q'/'Q' (or an S9 record), which the receiver recognises at
 * any point in that wait loop and answers with a "break s1 last rec"
 * message + the address it stopped at. So after streaming the data,
 * this sends a trailing 'q' and reads back whatever the board replies,
 * displaying it here - that reply is the actual proof the transfer was
 * received, since without it the board is left silently swallowing
 * anything typed afterwards (looking indistinguishable from a hang).
 *
 * Syntax: s1port <start> <end> [2400|9600|19200|28800|38400|76800|153600]
 *
 */

void do_s1port(void)
{
  int32  start, stop;
  speed_t baud;
  char   baudtok[8];
  char   path[80];
  char   line[100];
  int    fd;
  FILE   *out;
  fd_set rfds;
  struct timeval tv;
  char   resp[256];
  int    n, total;

  start = getadr();
  stop  = getadr();

  if (term_parse_baud(&baud, baudtok, sizeof(baudtok)) != 0)
  {
    prtmsg("Usage: s1port <start> <end> [2400|9600|19200|28800|38400|76800|153600]\n");
    return;
  }

  fd = term_open_device(baud, path, sizeof(path));
  if (fd < 0)
    return;

  out = fdopen(fd, "w");
  if (out == NULL)
  {
    prtmsg("fdopen failed\n");
    close(fd);
    return;
  }

  snprintf(line, sizeof(line), "Sending $%04X-$%04X to %s at %s baud...\n",
           (unsigned)start, (unsigned)stop, path, baudtok);
  prtmsg(line);

  s1_emit(out, S1PORT_CHAR_DELAY_US, 0, start, stop, NULL, 0);

  /*
   * The board's s1load receiver never exits on its own after the last
   * data byte of an S1 record - it loops back to wait for another 'S'
   * record and just sits there silently. The only way to get it back
   * to the monitor prompt (and to make it print a confirmation) is to
   * send 'q', which it recognises at any point in that wait loop. Send
   * it here and read back whatever the board says in response, so the
   * user gets direct proof the transfer was received rather than just
   * trusting a client-side "Done."
   */
  s1_putc(out, S1PORT_CHAR_DELAY_US, 'q');
  fflush(out);

  total = 0;
  for (;;)
  {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
      break;
    if (total >= (int)sizeof(resp) - 1)
      break;
    n = read(fd, resp + total, sizeof(resp) - 1 - total);
    if (n <= 0)
      break;
    total += n;
  }
  resp[total] = '\0';

  fclose(out);   /* also closes fd */

  if (total > 0)
  {
    prtmsg("Board response:\n");
    prtmsg(resp);
    prtmsg("\n");
  }
  else
  {
    prtmsg("No response from board after 'q' - it may not have been in s1load mode.\n");
  }

  prtmsg("Done.\n");
}

/*
 *
 * Copy a file from this machine straight to a USB-serial device, byte
 * for byte - no S-record framing, no reinterpretation of the
 * contents. Otherwise identical to s1port: same device picker (see
 * term_open_device), same per-character pacing (see
 * S1PORT_CHAR_DELAY_US), and the same trailing 'q' + board-response
 * readback for proof the board actually got it. Useful for sending an
 * S-record file produced by an external cross-assembler (or any other
 * file the board's own receiver understands) without first loading it
 * into this monitor's simulated memory.
 *
 * Syntax: sendfile <file> [2400|9600|19200|28800|38400|76800|153600]
 *
 */

void do_sendfile(void)
{
  char   fname[128];
  speed_t baud;
  char   baudtok[8];
  char   path[80];
  char   line[100];
  int    fd, i, ch;
  FILE   *in;
  FILE   *out;
  fd_set rfds;
  struct timeval tv;
  char   resp[256];
  int    n, total;

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    prtmsg("Usage: sendfile <file> [2400|9600|19200|28800|38400|76800|153600]\n");
    return;
  }
  i = 0;
  while (inbuf[pos] != 0 && inbuf[pos] != ' ' && i < (int)sizeof(fname) - 1)
    fname[i++] = inbuf[pos++];
  fname[i] = '\0';

  if (term_parse_baud(&baud, baudtok, sizeof(baudtok)) != 0)
  {
    prtmsg("Usage: sendfile <file> [2400|9600|19200|28800|38400|76800|153600]\n");
    return;
  }

  in = fopen(fname, "rb");
  if (in == NULL)
  {
    prtmsg("The file does not exist\n");
    return;
  }

  fd = term_open_device(baud, path, sizeof(path));
  if (fd < 0)
  {
    fclose(in);
    return;
  }

  out = fdopen(fd, "w");
  if (out == NULL)
  {
    prtmsg("fdopen failed\n");
    fclose(in);
    close(fd);
    return;
  }

  snprintf(line, sizeof(line), "Sending %s to %s at %s baud...\n",
           fname, path, baudtok);
  prtmsg(line);

  while ((ch = fgetc(in)) != EOF)
    s1_putc(out, S1PORT_CHAR_DELAY_US, ch);
  fclose(in);

  /* Same rationale as s1port: send 'q' and read back whatever the
     board says, since it won't return to its own prompt on its own. */
  s1_putc(out, S1PORT_CHAR_DELAY_US, 'q');
  fflush(out);

  total = 0;
  for (;;)
  {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
      break;
    if (total >= (int)sizeof(resp) - 1)
      break;
    n = read(fd, resp + total, sizeof(resp) - 1 - total);
    if (n <= 0)
      break;
    total += n;
  }
  resp[total] = '\0';

  fclose(out);   /* also closes fd */

  if (total > 0)
  {
    prtmsg("Board response:\n");
    prtmsg(resp);
    prtmsg("\n");
  }
  else
  {
    prtmsg("No response from board after 'q' - it may not have been in s1load mode.\n");
  }

  prtmsg("Done.\n");
}

/*
 *
 * Execute a shell command.
 *
 * Syntax: sys <shell command>
 *
 * The terminal is restored to normal mode before running the command
 * and put back into raw mode afterwards, so the command's output
 * renders correctly.
 *
 * Example: sys ls -la
 *          sys cat myfile.txt
 *
 */

void do_sys(void)
{
  int ret;

  ignblk();
  scancr();

  if (inbuf[pos] == 0)
  {
    prtmsg("Usage: sys <shell command>\n");
    return;
  }

  /* Restore normal terminal mode so the command gets a sane tty */
  term_restore();

  ret = system((char *)&inbuf[pos]);

  /* Back to raw mode for mgmon */
  term_raw();

  if (ret != 0)
  {
    prtmsg("Command failed.\n");
  }
}

/*
 *
 * Run a series of mgmon commands from a host text file, one per line,
 * exactly as if each had been typed at the Mgmon prompt: each line is
 * copied into inbuf in the CR,CR-terminated format buffer() itself
 * builds (see buffer()'s comment above its Return-key handling), then
 * run through lower()/sekcom()/dispatch() - the same path shell() uses
 * for a typed command, rather than a separate parser. A script line
 * can therefore be any command, including a nested "exec" of another
 * file.
 *
 * Blank lines, and lines whose first non-blank character is #, ; or *,
 * are treated as comments - skipped entirely, without an echo or a
 * trip through dispatch. "exit" inside a script is a no-op here
 * exactly as it is when typed directly - do_exit() itself does
 * nothing; only shell()'s own do-while loop (which never sees this
 * inner funktion value) actually ends the program, so a script can't
 * accidentally quit the monitor out from under the caller.
 *
 * Syntax: exec <file>
 *
 */

void do_exec(void)
{
  FILE *f;
  char  rawline[128];
  int   len;
  int   k;
  int   funktion;

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    prtmsg("Usage: exec <file>\n");
    return;
  }

  f = fopen((char *)&inbuf[pos], "r");
  if (f == NULL)
  {
    prtmsg("The file does not exist\n");
    return;
  }

  while (fgets(rawline, sizeof(rawline), f) != NULL)
  {
    len = (int)strlen(rawline);
    while (len > 0 && (rawline[len-1] == '\n' || rawline[len-1] == '\r'))
      rawline[--len] = '\0';
    if (len > maxtkn)
      len = maxtkn;      /* same cap buffer()'s line editor enforces */

    k = 0;
    while (rawline[k] == ' ' || rawline[k] == '\t')
      k++;
    if (rawline[k] == '\0' || rawline[k] == '#' || rawline[k] == ';' || rawline[k] == '*')
      continue;

    prtmsg("\nMgmon> ");
    prtmsg(rawline);
    prtchr('\n');

    memcpy(inbuf, rawline, len);
    inbuf[len]   = CR;
    inbuf[len+1] = CR;
    pos = 0;

    lower();
    funktion = sekcom();
    if (funktion == -1)
      prtmsg("Undefined command, try help!\n");
    else if ((funktion > 0) && (funktion <= MAXCOMMAND))
      dispatch(funktion);
  }

  fclose(f);
}


void scancr(void )

{
  int i;
   for(i = 0; i < maxtkn; i++)
     if(inbuf[i] == CR)
        inbuf[i] = 0;
}

/*
 *
 * Read one 256-byte sector of the currently open FLEX disk image.
 * Returns 0 on success, -1 on failure (no image open, or short read).
 *
 */

static int flex_read_sector(int track, int sector, byte *buf)
{
  long offset;

  if (dsk_fil == NULL)
    return(-1);
  offset = ((long)track * dsk_max_sector + (sector - 1)) * FLEX_SECSZ;
  if (fseek(dsk_fil, offset, SEEK_SET) != 0)
    return(-1);
  if (fread(buf, 1, FLEX_SECSZ, dsk_fil) != FLEX_SECSZ)
    return(-1);
  return(0);
}

/*
 *
 * Write one 256-byte sector to the currently open FLEX disk image.
 * Returns 0 on success, -1 on failure (no image open, image opened
 * read-only, or short write).
 *
 */

static int flex_write_sector(int track, int sector, const byte *buf)
{
  long offset;

  if (dsk_fil == NULL || dsk_readonly)
    return(-1);

  if (flex_debug)
  {
    char line[64];
    snprintf(line, sizeof(line), "Writing track %d sector %d: %02X %02X %02X %02X\n",
             track, sector, buf[0], buf[1], buf[2], buf[3]);
    prtmsg(line);
  }

  offset = ((long)track * dsk_max_sector + (sector - 1)) * FLEX_SECSZ;
  if (fseek(dsk_fil, offset, SEEK_SET) != 0)
    return(-1);
  if (fwrite(buf, 1, FLEX_SECSZ, dsk_fil) != FLEX_SECSZ)
    return(-1);
  fflush(dsk_fil);
  return(0);
}

/*
 *
 * Copy a fixed-width space-padded directory field into a NUL-terminated
 * string, trimming trailing spaces.
 *
 */

static void flex_field_to_str(char *dst, const byte *src, int len)
{
  int i;

  memcpy(dst, src, len);
  dst[len] = '\0';
  for (i = len - 1; i >= 0 && dst[i] == ' '; i--)
    dst[i] = '\0';
}

/*
 *
 * Expand a directory entry's single-byte year (0-99, all FLEX ever
 * stored) into a 4-digit year for display. Uses the same 75-year
 * pivot flexemu adopted for Y2K (its ChangeLog v1.25, 2000-03-24):
 * byte < 75 -> 2000-2074, byte >= 75 -> 1975-1999. Matches every
 * existing image already on disk (this port never writes a year
 * outside 1975-2074 - see do_fsave/flexinject, which store tm_year %
 * 100) rather than inventing an incompatible encoding of our own.
 *
 */

static int flex_year4(int byte_year)
{
  return((byte_year < 75) ? 2000 + byte_year : 1900 + byte_year);
}

/*
 *
 * Parse a "name.ext" argument from the command line into fixed-width,
 * space-padded 8+3 fields matching the on-disk directory entry format.
 * Returns 0 if no filename argument was given.
 *
 */

static int flex_parse_filename(char *base, char *ext)
{
  int j, k;

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
    return(0);

  memset(base, ' ', 8);
  memset(ext,  ' ', 3);

  j = pos;
  k = 0;
  while (inbuf[j] != 0 && inbuf[j] != '.')
  {
    if (k < 8)
      base[k++] = inbuf[j];
    j++;
  }
  if (inbuf[j] == '.')
  {
    j++;
    k = 0;
    while (inbuf[j] != 0 && k < 3)
      ext[k++] = inbuf[j++];
  }
  pos = j;   /* so callers can parse further arguments after name.ext */
  return(1);
}

/*
 *
 * Blank-insensitive, case-insensitive character comparison for FLEX
 * directory name/extension fields: real disk images pad the unused
 * tail of a short name with 0x00 as often as with a space, so both
 * are treated as equivalent to the space used by flex_parse_filename().
 *
 */

static int flex_pad_eq(int a, int b)
{
  if (a == 0) a = ' ';
  if (b == 0) b = ' ';
  return(toupper(a) == toupper(b));
}

/*
 *
 * Case-insensitive comparison of a directory entry's 8+3 name field
 * against a parsed base/ext pair from flex_parse_filename().
 *
 */

static int flex_name_eq(const byte *entry, const char *base, const char *ext)
{
  int i;

  for (i = 0; i < 8; i++)
    if (!flex_pad_eq(entry[i], (unsigned char)base[i]))
      return(0);
  for (i = 0; i < 3; i++)
    if (!flex_pad_eq(entry[8 + i], (unsigned char)ext[i]))
      return(0);
  return(1);
}

/*
 *
 * Search the FLEX directory chain for a file matching base/ext.
 * On success returns 0 and sets st_t and st_s to its start track/sector.
 *
 */

static int flex_find_entry(const char *base, const char *ext, int *st_t, int *st_s)
{
  byte dirsec[FLEX_SECSZ];
  int  track, sector, entry, off;

  track  = 0;
  sector = 5;
  while (track != 0 || sector != 0)
  {
    if (flex_read_sector(track, sector, dirsec) != 0)
      return(-1);
    for (entry = 0; entry < 10; entry++)
    {
      off = 16 + entry * 24;
      if (dirsec[off] == 0 || (dirsec[off] & 0x80))
        continue;
      if (flex_name_eq(&dirsec[off], base, ext))
      {
        *st_t = dirsec[off + 13];
        *st_s = dirsec[off + 14];
        return(0);
      }
    }
    track  = dirsec[0];
    sector = dirsec[1];
  }
  return(-1);
}

/*
 *
 * Same search as flex_find_entry(), but for callers that need to
 * modify the entry in place (fdelete, frename) rather than just read
 * its file data: also returns the directory sector's own location
 * (dir_t/dir_s) and byte offset within it (dir_off), plus the end
 * track/sector and record count fields needed to return the file's
 * sectors to the free chain.
 *
 */

static int flex_find_entry_full(const char *base, const char *ext,
                                 int *dir_t, int *dir_s, int *dir_off,
                                 int *st_t, int *st_s,
                                 int *en_t, int *en_s, int *records)
{
  byte dirsec[FLEX_SECSZ];
  int  track, sector, entry, off;

  track  = 0;
  sector = 5;
  while (track != 0 || sector != 0)
  {
    if (flex_read_sector(track, sector, dirsec) != 0)
      return(-1);
    for (entry = 0; entry < 10; entry++)
    {
      off = 16 + entry * 24;
      if (dirsec[off] == 0 || (dirsec[off] & 0x80))
        continue;
      if (flex_name_eq(&dirsec[off], base, ext))
      {
        *dir_t   = track;
        *dir_s   = sector;
        *dir_off = off;
        *st_t    = dirsec[off + 13];
        *st_s    = dirsec[off + 14];
        *en_t    = dirsec[off + 15];
        *en_s    = dirsec[off + 16];
        *records = dirsec[off + 17] * 256 + dirsec[off + 18];
        return(0);
      }
    }
    track  = dirsec[0];
    sector = dirsec[1];
  }
  return(-1);
}

/*
 *
 * Open a FLEX 6809 .dsk disk image and remember it for later commands
 * (fcat, flist, fsave, sector). Reads the System Information Record
 * to learn the disk geometry and volume label. Opened read/write when
 * possible so fsave can allocate and write file data; falls back to
 * read-only if the underlying file can't be written.
 *
 * Syntax: dskfile [file.dsk]
 *
 * Called with no argument, reports the currently selected image (if
 * any) and its geometry/free space instead of opening a new one.
 *
 */

void do_dskfile(void)
{
  byte sir[FLEX_SECSZ];
  FILE *fp;
  char line[80];
  int  free_sectors;

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    if (dsk_fil == NULL)
    {
      prtmsg("No disk image open. Usage: dskfile <file.dsk>\n");
      return;
    }
    snprintf(line, sizeof(line), "Current image: %s  volume: %s%s\n", dsk_name,
             dsk_label, dsk_readonly ? "  (read-only)" : "");
    prtmsg(line);
    snprintf(line, sizeof(line), "Tracks: 0-%d  Sectors/track: %d\n",
             dsk_max_track, dsk_max_sector);
    prtmsg(line);
    if (flex_read_sector(0, 3, sir) == 0)
    {
      free_sectors = sir[33] * 256 + sir[34];
      snprintf(line, sizeof(line), "Free sectors: %d\n", free_sectors);
      prtmsg(line);
    }
    return;
  }

  fp = fopen((char *)&inbuf[pos], "r+b");
  dsk_readonly = 0;
  if (fp == NULL)
  {
    fp = fopen((char *)&inbuf[pos], "rb");
    dsk_readonly = 1;
  }
  if (fp == NULL)
  {
    prtmsg("The file does not exist\n");
    return;
  }

  if (dsk_fil != NULL)
    fclose(dsk_fil);
  dsk_fil = fp;
  strncpy(dsk_name, (char *)&inbuf[pos], sizeof(dsk_name) - 1);
  dsk_name[sizeof(dsk_name) - 1] = '\0';
  dsk_last_track = dsk_last_sector = -1;
  dsk_last_link_t = dsk_last_link_s = -1;

  dsk_max_sector = 0;   /* track 0 offsets don't depend on this yet */
  if (flex_read_sector(0, 3, sir) != 0)
  {
    prtmsg("Could not read System Information Record\n");
    fclose(dsk_fil);
    dsk_fil = NULL;
    return;
  }

  dsk_max_track  = sir[38];
  dsk_max_sector = sir[39];

  flex_field_to_str(dsk_label, &sir[16], 11);

  snprintf(line, sizeof(line), "Opened %s  volume: %s%s\n", dsk_name, dsk_label,
           dsk_readonly ? "  (read-only)" : "");
  prtmsg(line);
  snprintf(line, sizeof(line), "Tracks: 0-%d  Sectors/track: %d\n",
           dsk_max_track, dsk_max_sector);
  prtmsg(line);
}

/*
 *
 * List the FLEX directory of the disk image most recently opened with
 * dskfile. Takes no arguments - it remembers the selected image.
 *
 * Syntax: fcat
 *
 */

void do_fcat(void)
{
  byte dirsec[FLEX_SECSZ];
  byte sir[FLEX_SECSZ];
  char fname[9], fext[4];
  char line[80];
  int  track, sector, entry, base;
  int  count, free_sectors;

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }

  snprintf(line, sizeof(line), "Volume: %s\n", dsk_label);
  prtmsg(line);
  prtmsg("Name       Ext  Sectors  Start   End    Date\n");

  count  = 0;
  track  = 0;
  sector = 5;

  while (track != 0 || sector != 0)
  {
    if (flex_read_sector(track, sector, dirsec) != 0)
    {
      prtmsg("Error reading directory sector\n");
      break;
    }

    for (entry = 0; entry < 10; entry++)
    {
      base = 16 + entry * 24;
      if (dirsec[base] == 0 || (dirsec[base] & 0x80))
        continue;

      flex_field_to_str(fname, &dirsec[base], 8);
      flex_field_to_str(fext, &dirsec[base + 8], 3);

      snprintf(line, sizeof(line),
               "%-8s   %-3s  %5d   %2d/%-2d - %2d/%-2d  %02d/%02d/%04d\n",
               fname, fext,
               dirsec[base + 17] * 256 + dirsec[base + 18],
               dirsec[base + 13], dirsec[base + 14],
               dirsec[base + 15], dirsec[base + 16],
               dirsec[base + 21], dirsec[base + 22],
               flex_year4(dirsec[base + 23]));
      prtmsg(line);
      count++;
    }

    track  = dirsec[0];
    sector = dirsec[1];
  }

  if (flex_read_sector(0, 3, sir) == 0)
  {
    free_sectors = sir[33] * 256 + sir[34];
    snprintf(line, sizeof(line), "%d file(s), %d free sector(s)\n",
             count, free_sectors);
    prtmsg(line);
  }
}

/*
 *
 * List a FLEX text file from the open disk image to the terminal.
 * Decodes the on-disk text conventions used by the TSC line editor:
 * NUL and 0x18 bytes are padding/markers and are dropped, CR (0x0D)
 * starts a new line, and 0x09 introduces a run-length-encoded block
 * of spaces (the following byte gives the space count).
 *
 * Syntax: flist <name.ext>
 *
 */

void do_flist(void)
{
  char base[8], ext[3];
  byte datasec[FLEX_SECSZ];
  int  track, sector, next_t, next_s, i;
  int  expect_count;
  byte ch;

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }

  if (!flex_parse_filename(base, ext))
  {
    prtmsg("Usage: flist <name.ext>\n");
    return;
  }

  if (flex_find_entry(base, ext, &track, &sector) != 0)
  {
    prtmsg("File not found\n");
    return;
  }

  expect_count = 0;
  while (track != 0 || sector != 0)
  {
    if (flex_read_sector(track, sector, datasec) != 0)
    {
      prtmsg("Error reading data sector\n");
      break;
    }
    next_t = datasec[0];
    next_s = datasec[1];

    for (i = 4; i < FLEX_SECSZ; i++)
    {
      ch = datasec[i];
      if (expect_count)
      {
        int k;
        for (k = 0; k < ch; k++)
          prtchr(' ');
        expect_count = 0;
        continue;
      }
      if (ch == 0 || ch == 0x18)
        continue;
      if (ch == 0x0d)
      {
        prtchr('\n');
        continue;
      }
      if (ch == 0x09)
      {
        expect_count = 1;
        continue;
      }
      {
        char pr = ch & 0x7f;
        if (pr < 32)
          pr = '.';
        prtchr(pr);
      }
    }

    track  = next_t;
    sector = next_s;
  }
  prtchr('\n');
}

/*
 *
 * Copy a FLEX text file from the open disk image to a file on the
 * host (Linux) filesystem. Decodes the same on-disk conventions as
 * flist (see the comment above it), but writes a plain Unix newline
 * (0x0A) for each FLEX end-of-line marker (CR, 0x0D) instead of the
 * original byte, so the result is a proper Linux text file rather
 * than a copy with stray carriage returns.
 *
 * Syntax: flex2file <name.ext> <hostfile>
 *
 */

void do_flex2file(void)
{
  char  base[8], ext[3];
  byte  datasec[FLEX_SECSZ];
  int   track, sector, next_t, next_s, i, k;
  int   expect_count;
  byte  ch;
  FILE  *outfp;
  long  bytes_written;
  char  line[100];

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }

  if (!flex_parse_filename(base, ext))
  {
    prtmsg("Usage: flex2file <name.ext> <hostfile>\n");
    return;
  }

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    prtmsg("Usage: flex2file <name.ext> <hostfile>\n");
    return;
  }

  if (flex_find_entry(base, ext, &track, &sector) != 0)
  {
    prtmsg("File not found\n");
    return;
  }

  if (flex_read_sector(track, sector, datasec) != 0)
  {
    prtmsg("Error reading data sector\n");
    return;
  }
  if (datasec[4] == 0x02)
  {
    prtmsg("This looks like a binary file, not text - use fget instead\n");
    return;
  }

  outfp = fopen((char *)&inbuf[pos], "wb");
  if (outfp == NULL)
  {
    prtmsg("Could not create the output file\n");
    return;
  }

  bytes_written = 0;
  expect_count  = 0;
  while (track != 0 || sector != 0)
  {
    if (flex_read_sector(track, sector, datasec) != 0)
    {
      prtmsg("Error reading data sector\n");
      break;
    }
    next_t = datasec[0];
    next_s = datasec[1];

    for (i = 4; i < FLEX_SECSZ; i++)
    {
      ch = datasec[i];
      if (expect_count)
      {
        for (k = 0; k < ch; k++)
        {
          fputc(' ', outfp);
          bytes_written++;
        }
        expect_count = 0;
        continue;
      }
      if (ch == 0 || ch == 0x18)
        continue;
      if (ch == 0x0d)
      {
        fputc('\n', outfp);
        bytes_written++;
        continue;
      }
      if (ch == 0x09)
      {
        expect_count = 1;
        continue;
      }
      fputc(ch & 0x7f, outfp);
      bytes_written++;
    }

    track  = next_t;
    sector = next_s;
  }

  fclose(outfp);
  snprintf(line, sizeof(line), "Copied %ld bytes to %s\n", bytes_written, (char *)&inbuf[pos]);
  prtmsg(line);
}

/*
 *
 * Decode a FLEX text file (same on-disk convention as flist, see the
 * comment above it) into memory starting at load_addr. Unlike flist,
 * control characters are not replaced with '.' - the exact decoded
 * bytes are written since this is a load, not a display. Line breaks
 * are written as the original CR (0x0D), matching how the text would
 * appear in memory on real FLEX hardware. Returns the address just
 * past the last byte written.
 *
 */

static unint16 flex_load_text(int track, int sector, unint16 load_addr)
{
  byte  datasec[FLEX_SECSZ];
  int   next_t, next_s, i, k;
  int   expect_count;
  byte  ch;
  int32 addr;

  addr = load_addr;
  expect_count = 0;
  while (track != 0 || sector != 0)
  {
    if (flex_read_sector(track, sector, datasec) != 0)
    {
      prtmsg("Error reading data sector\n");
      break;
    }
    next_t = datasec[0];
    next_s = datasec[1];

    for (i = 4; i < FLEX_SECSZ; i++)
    {
      ch = datasec[i];
      if (expect_count)
      {
        for (k = 0; k < ch; k++)
          memw(addr++, ' ');
        expect_count = 0;
        continue;
      }
      if (ch == 0 || ch == 0x18)
        continue;
      if (ch == 0x0d)
      {
        memw(addr++, 0x0d);
        continue;
      }
      if (ch == 0x09)
      {
        expect_count = 1;
        continue;
      }
      memw(addr++, ch & 0x7f);
    }

    track  = next_t;
    sector = next_s;
  }
  return((unint16)addr);
}

/*
 *
 * Sequential byte reader over a FLEX file's data sector chain, hiding
 * the 4-byte link/sequence header of each sector so a caller can just
 * pull the file's raw content one byte at a time.
 *
 */

struct flex_bstream
{
  byte buf[FLEX_SECSZ];
  int  pos;
  int  eof;
};

static void flex_bstream_open(struct flex_bstream *bs, int track, int sector)
{
  bs->pos = FLEX_SECSZ;   /* force a sector read on the first getc() */
  bs->eof = 0;
  if ((track == 0 && sector == 0) || flex_read_sector(track, sector, bs->buf) != 0)
  {
    bs->eof = 1;
    return;
  }
  bs->pos = 4;
}

static int flex_bstream_getc(struct flex_bstream *bs)
{
  int next_t, next_s;

  if (bs->eof)
    return(-1);
  if (bs->pos >= FLEX_SECSZ)
  {
    next_t = bs->buf[0];
    next_s = bs->buf[1];
    if ((next_t == 0 && next_s == 0) || flex_read_sector(next_t, next_s, bs->buf) != 0)
    {
      bs->eof = 1;
      return(-1);
    }
    bs->pos = 4;
  }
  return(bs->buf[(bs->pos)++]);
}

/*
 *
 * Load a FLEX binary (load module) file into memory. Such a file is a
 * sequence of records over its raw data stream:
 *
 *   0x02 <addr-hi> <addr-lo> <count> <count data bytes>   - data record
 *   0x16 <addr-hi> <addr-lo>                              - transfer record, ends the file
 *
 * Each data record is written at its own embedded address, so a file
 * may load into several disjoint areas of memory. Adjacent records
 * (whose address picks up exactly where the previous one left off)
 * are merged into a single reported range.
 *
 */

static void flex_load_binary(int track, int sector)
{
  struct flex_bstream bs;
  int   mark, hi, lo, count, i, byte_val;
  int32 rec_addr, range_start, range_end;
  int32 xfer_addr;
  char  line[40];

  flex_bstream_open(&bs, track, sector);
  range_start = range_end = -1;
  xfer_addr = -1;

  while ((mark = flex_bstream_getc(&bs)) != -1)
  {
    if (mark == 0x16)   /* transfer address record: end of file */
    {
      hi = flex_bstream_getc(&bs);
      lo = flex_bstream_getc(&bs);
      if (hi >= 0 && lo >= 0)
        xfer_addr = hi * 0x100 + lo;
      break;
    }
    if (mark != 0x02)   /* not a record we understand - stop here */
      break;

    hi    = flex_bstream_getc(&bs);
    lo    = flex_bstream_getc(&bs);
    count = flex_bstream_getc(&bs);
    if (hi < 0 || lo < 0 || count < 0)
      break;
    rec_addr = hi * 0x100 + lo;

    if (range_start >= 0 && rec_addr != range_end + 1)
    {
      prtmsg("  $");
      prtadr(range_start);
      prtmsg(" - $");
      prtadr(range_end);
      prtmsg("\n");
      range_start = -1;
    }
    if (range_start < 0)
      range_start = rec_addr;

    for (i = 0; i < count; i++)
    {
      byte_val = flex_bstream_getc(&bs);
      if (byte_val < 0)
        break;
      memw(rec_addr + i, byte_val);
    }
    range_end = rec_addr + i - 1;
  }

  if (range_start >= 0)
  {
    prtmsg("  $");
    prtadr(range_start);
    prtmsg(" - $");
    prtadr(range_end);
    prtmsg("\n");
  }
  if (xfer_addr >= 0)
  {
    snprintf(line, sizeof(line), "Transfer address: $%04X\n", (unsigned)xfer_addr);
    prtmsg(line);
  }
}

/*
 *
 * Load a file from the open FLEX disk image into memory. Whether the
 * file is a FLEX binary (load module) or a text file is auto-detected
 * from its first data byte: 0x02 is the data-record marker used by
 * every FLEX binary, so a file that starts with anything else is
 * treated as text.
 *
 * A binary file's records carry their own load addresses, so it is
 * always loaded wherever it says; every contiguous range written is
 * listed. A text file has no embedded address, so it is decoded (as
 * flist would display it) starting at 0x0000, or at the address given
 * as an optional second argument.
 *
 * Syntax: fget <name.ext> [addr]
 *
 */

void do_fget(void)
{
  char  base[8], ext[3];
  byte  datasec[FLEX_SECSZ];
  int   track, sector;
  int32 addr;
  unint16 end_addr;
  char  line[40];

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }

  if (!flex_parse_filename(base, ext))
  {
    prtmsg("Usage: fget <name.ext> [addr]\n");
    return;
  }

  ignblk();
  scancr();
  addr = (inbuf[pos] != 0) ? getadr() : 0;

  if (flex_find_entry(base, ext, &track, &sector) != 0)
  {
    prtmsg("File not found\n");
    return;
  }

  if (flex_read_sector(track, sector, datasec) != 0)
  {
    prtmsg("Error reading data sector\n");
    return;
  }

  if (datasec[4] == 0x02)
  {
    prtmsg("Binary file, loaded at:\n");
    flex_load_binary(track, sector);
  }
  else
  {
    end_addr = flex_load_text(track, sector, (unint16)addr);
    snprintf(line, sizeof(line), "Text file, loaded $%04X-$%04X\n",
             (unsigned)addr, (unsigned)(end_addr - 1));
    prtmsg(line);
  }
}

#define FLEX_MAX_ALLOC  1024

/* Shared scratch buffer for building a new file's data before handing
   it to flex_create_file() - big enough for the largest file that
   fits FLEX_MAX_ALLOC sectors. Used by both do_fsave (copied in from
   simulated memory) and do_flexinject (filled by the text encoder).
   Never in use by both at once - this program is single-threaded and
   each caller fills it fresh immediately before use. */
static byte flex_iobuf[FLEX_MAX_ALLOC * (FLEX_SECSZ - 4)];

/*
 *
 * Allocate sectors from the disk's free chain (recorded in the SIR),
 * write data across them, and add a directory entry - the common core
 * shared by fsave (data from simulated memory) and flexinject (data
 * from a host file, already space-compression-encoded). Caller has
 * already checked dsk_fil/dsk_readonly and that base/ext doesn't
 * already exist.
 *
 * Returns the number of sectors used on success, or -1 (after
 * printing its own message) on failure.
 *
 */

static int flex_create_file(const byte *data, int32 total_bytes, const char *base, const char *ext)
{
  int   sectors_needed, free_count;
  int   cur_t, cur_s;
  int   alloc_t[FLEX_MAX_ALLOC], alloc_s[FLEX_MAX_ALLOC];
  byte  tmp[FLEX_SECSZ], sir[FLEX_SECSZ], dirsec[FLEX_SECSZ];
  int   dir_track, dir_sector, dir_off, found_slot;
  int   i, j, copy_len, remain;
  time_t     now;
  struct tm *tmv;

  if (total_bytes <= 0)
  {
    prtmsg("Nothing to save\n");
    return(-1);
  }

  sectors_needed = (total_bytes + (FLEX_SECSZ - 4) - 1) / (FLEX_SECSZ - 4);
  if (sectors_needed > FLEX_MAX_ALLOC)
  {
    prtmsg("File too large\n");
    return(-1);
  }

  if (flex_read_sector(0, 3, sir) != 0)
  {
    prtmsg("Could not read System Information Record\n");
    return(-1);
  }
  free_count = sir[33] * 256 + sir[34];
  if (free_count < sectors_needed)
  {
    prtmsg("Not enough free space on disk image\n");
    return(-1);
  }

  /* Walk the free chain, collecting the sectors we will use. */
  cur_t = sir[29];
  cur_s = sir[30];
  for (i = 0; i < sectors_needed; i++)
  {
    if (cur_t == 0 && cur_s == 0)
    {
      prtmsg("Free chain exhausted unexpectedly\n");
      return(-1);
    }
    alloc_t[i] = cur_t;
    alloc_s[i] = cur_s;
    if (flex_read_sector(cur_t, cur_s, tmp) != 0)
    {
      prtmsg("Error reading free-chain sector\n");
      return(-1);
    }
    cur_t = tmp[0];
    cur_s = tmp[1];
  }
  /* cur_t/cur_s is now the new free-chain head after allocation. */

  /* Find a free directory slot in the existing directory chain. */
  found_slot = 0;
  dir_track  = 0;
  dir_sector = 5;
  dir_off    = 0;
  while (!found_slot && (dir_track != 0 || dir_sector != 0))
  {
    if (flex_read_sector(dir_track, dir_sector, dirsec) != 0)
    {
      prtmsg("Error reading directory sector\n");
      return(-1);
    }
    for (j = 0; j < 10; j++)
    {
      dir_off = 16 + j * 24;
      if (dirsec[dir_off] == 0 || (dirsec[dir_off] & 0x80))
      {
        found_slot = 1;
        break;
      }
    }
    if (!found_slot)
    {
      if (dirsec[0] == 0 && dirsec[1] == 0)
        break;
      dir_track  = dirsec[0];
      dir_sector = dirsec[1];
    }
  }
  if (!found_slot)
  {
    prtmsg("Directory is full\n");
    return(-1);
  }

  /* Write the file's data sectors. Bytes 2-3 of each sector are a
     1-based sequential record number within this file's own chain
     (confirmed against a real dsktool -c consistency check - it was
     previously left zero here, which dsktool flags as a WRECNR
     warning, not a hard error, but every sector should carry it). */
  for (i = 0; i < sectors_needed; i++)
  {
    memset(tmp, 0, FLEX_SECSZ);
    if (i < sectors_needed - 1)
    {
      tmp[0] = (byte)alloc_t[i + 1];
      tmp[1] = (byte)alloc_s[i + 1];
    }
    tmp[2] = (byte)((i + 1) >> 8);
    tmp[3] = (byte)((i + 1) & 0xff);
    remain   = total_bytes - i * (FLEX_SECSZ - 4);
    copy_len = (remain > (FLEX_SECSZ - 4)) ? (FLEX_SECSZ - 4) : remain;
    for (j = 0; j < copy_len; j++)
      tmp[4 + j] = data[i * (FLEX_SECSZ - 4) + j];

    if (flex_write_sector(alloc_t[i], alloc_s[i], tmp) != 0)
    {
      prtmsg("Error writing data sector\n");
      return(-1);
    }
  }

  /* Advance the SIR's free-chain head and free-sector count. */
  sir[29] = (byte)cur_t;
  sir[30] = (byte)cur_s;
  if (cur_t == 0 && cur_s == 0)
  {
    sir[31] = 0;
    sir[32] = 0;
  }
  free_count -= sectors_needed;
  sir[33] = (byte)(free_count >> 8);
  sir[34] = (byte)(free_count & 0xff);
  if (flex_write_sector(0, 3, sir) != 0)
  {
    prtmsg("Error updating System Information Record\n");
    return(-1);
  }

  /* Write the new directory entry. */
  memcpy(&dirsec[dir_off],     base, 8);
  memcpy(&dirsec[dir_off + 8], ext,  3);
  dirsec[dir_off + 11] = 0;
  dirsec[dir_off + 12] = 0;
  dirsec[dir_off + 13] = (byte)alloc_t[0];
  dirsec[dir_off + 14] = (byte)alloc_s[0];
  dirsec[dir_off + 15] = (byte)alloc_t[sectors_needed - 1];
  dirsec[dir_off + 16] = (byte)alloc_s[sectors_needed - 1];
  dirsec[dir_off + 17] = (byte)(sectors_needed >> 8);
  dirsec[dir_off + 18] = (byte)(sectors_needed & 0xff);
  dirsec[dir_off + 19] = 0;
  dirsec[dir_off + 20] = 0;

  now = time(NULL);
  tmv = localtime(&now);
  dirsec[dir_off + 21] = (byte)(tmv->tm_mon + 1);
  dirsec[dir_off + 22] = (byte)(tmv->tm_mday);
  dirsec[dir_off + 23] = (byte)(tmv->tm_year % 100);

  if (flex_write_sector(dir_track, dir_sector, dirsec) != 0)
  {
    prtmsg("Error writing directory sector\n");
    return(-1);
  }

  return(sectors_needed);
}

/*
 *
 * Save a memory range into the open FLEX disk image as a new file.
 * See flex_create_file() above for the actual allocate/write/commit
 * logic - this just copies the memory range into flex_iobuf first
 * (through memr(), the sanctioned memory-access path) and reports the
 * result.
 *
 * Syntax: fsave <start> <end> <name.ext>
 *
 */

void do_fsave(void)
{
  char  base[8], ext[3];
  int32 start, stop, total_bytes, i;
  int   exist_t, exist_s;
  int   sectors_needed;
  char  namebuf[9], extbuf[4];
  char  line[80];

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }
  if (dsk_readonly)
  {
    prtmsg("Disk image is read-only\n");
    return;
  }

  start = getadr();
  stop  = getadr();

  if (!flex_parse_filename(base, ext))
  {
    prtmsg("Usage: fsave <start> <end> <name.ext>\n");
    return;
  }

  total_bytes = stop - start;
  if (total_bytes <= 0)
  {
    prtmsg("Nothing to save\n");
    return;
  }

  if (flex_find_entry(base, ext, &exist_t, &exist_s) == 0)
  {
    prtmsg("File already exists\n");
    return;
  }

  if (total_bytes > (int32)sizeof(flex_iobuf))
  {
    prtmsg("File too large\n");
    return;
  }

  for (i = 0; i < total_bytes; i++)
    flex_iobuf[i] = memr(start + i) & 0xFF;

  sectors_needed = flex_create_file(flex_iobuf, total_bytes, base, ext);
  if (sectors_needed < 0)
    return;

  flex_field_to_str(namebuf, (byte *)base, 8);
  flex_field_to_str(extbuf,  (byte *)ext,  3);
  snprintf(line, sizeof(line), "Saved %ld bytes as %s.%s (%d sectors)\n",
           (long)total_bytes, namebuf, extbuf, sectors_needed);
  prtmsg(line);
}

/*
 *
 * Encode a host text file into FLEX's on-disk text convention - the
 * exact inverse of the decode loop in do_flist() above: a run of two
 * or more spaces becomes 0x09 followed by a count byte (capped at
 * 127, matching the real FLEX/TSC editor convention - a longer run is
 * flushed mid-way rather than ever emitting a count over 127); a
 * single space is a literal 0x20; a host LF becomes FLEX's CR (0x0D)
 * end-of-line marker; a host tab becomes the same space-run encoding
 * for 8 spaces (0x09 0x08); other bytes below 0x20 or at/above 0x80
 * are dropped.
 *
 * That last part matters for round-tripping: do_flex2file() masks
 * every literal byte with & 0x7f on the way back out to a host file
 * (see the comment above it), so a byte >= 0x80 written here could
 * never be read back unchanged anyway - dropping it here rather than
 * writing something that silently can't survive a round trip.
 *
 * Returns the encoded length on success, or -1 if the encoded output
 * would exceed max_len (the caller's buffer size) - checked before
 * every byte written, so the buffer is never overrun.
 *
 */

static int32 flex_encode_text(FILE *in, byte *out, int32 max_len)
{
  int32 len;
  int   ch, space_run;

  len = 0;
  space_run = 0;

  while ((ch = fgetc(in)) != EOF)
  {
    if (ch == ' ')
    {
      space_run++;
      if (space_run == 127)
      {
        if (len + 2 > max_len) return(-1);
        out[len++] = 0x09;
        out[len++] = 127;
        space_run = 0;
      }
      continue;
    }

    /* flush any pending run of spaces before a non-space byte */
    if (space_run == 1)
    {
      if (len + 1 > max_len) return(-1);
      out[len++] = ' ';
      space_run = 0;
    }
    else if (space_run > 1)
    {
      if (len + 2 > max_len) return(-1);
      out[len++] = 0x09;
      out[len++] = (byte)space_run;
      space_run = 0;
    }

    if (ch == '\n')
    {
      if (len + 1 > max_len) return(-1);
      out[len++] = 0x0D;
    }
    else if (ch == '\t')
    {
      if (len + 2 > max_len) return(-1);
      out[len++] = 0x09;
      out[len++] = 8;
    }
    else if (ch >= 0x20 && ch < 0x7F)
    {
      if (len + 1 > max_len) return(-1);
      out[len++] = (byte)ch;
    }
    /* else: control character or byte >= 0x80 - dropped, see above */
  }

  if (space_run == 1)
  {
    if (len + 1 > max_len) return(-1);
    out[len++] = ' ';
  }
  else if (space_run > 1)
  {
    if (len + 2 > max_len) return(-1);
    out[len++] = 0x09;
    out[len++] = (byte)space_run;
  }

  return(len);
}

/*
 *
 * Import a text file from this machine into the open FLEX disk image
 * as a new file. The FLEX 8.3 name/extension are derived from the
 * host filename (e.g. hello.txt -> HELLO.TXT, readme -> README with a
 * blank extension) - if either part doesn't fit, this refuses rather
 * than silently truncating, since a truncated name is easy to miss
 * and confusing to find again later.
 *
 * Syntax: flexinject <file>
 *
 */

void do_flexinject(void)
{
  char   base[8], ext[3];
  char   namebuf[9], extbuf[4];
  char   line[160];
  char   host_base[64];
  int    i, j, slash, dot, host_len;
  int    exist_t, exist_s;
  int    sectors_needed;
  int32  encoded_len;
  FILE  *in;

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }
  if (dsk_readonly)
  {
    prtmsg("Disk image is read-only\n");
    return;
  }

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    prtmsg("Usage: flexinject <file>\n");
    return;
  }

  /* Derive the FLEX name from the host path's basename: skip any
     directory components, then split on the last '.'. */
  host_len = 0;
  while (inbuf[pos + host_len] != 0)
    host_len++;
  slash = -1;
  for (i = 0; i < host_len; i++)
    if (inbuf[pos + i] == '/')
      slash = i;
  dot = -1;
  for (i = slash + 1; i < host_len; i++)
    if (inbuf[pos + i] == '.')
      dot = i;

  j = 0;
  for (i = slash + 1; (dot < 0 || i < dot) && i < host_len; i++)
    if (j < (int)sizeof(host_base) - 1)
      host_base[j++] = inbuf[pos + i];
  host_base[j] = '\0';

  if (j > 8)
  {
    snprintf(line, sizeof(line),
             "Name \"%s\" is %d characters - FLEX names are limited to 8. Nothing saved.\n",
             host_base, j);
    prtmsg(line);
    return;
  }
  memset(base, ' ', 8);
  memcpy(base, host_base, j);

  memset(ext, ' ', 3);
  if (dot >= 0)
  {
    j = 0;
    for (i = dot + 1; i < host_len; i++)
      if (j < 4)
        host_base[j++] = inbuf[pos + i];
    host_base[j] = '\0';
    if (j > 3)
    {
      snprintf(line, sizeof(line),
               "Extension \"%s\" is %d characters - FLEX extensions are limited to 3. Nothing saved.\n",
               host_base, j);
      prtmsg(line);
      return;
    }
    memcpy(ext, host_base, j);
  }

  /* inbuf is lowercased by lower() before dispatch, but real FLEX names
     are uppercase-only - upcase the derived name/ext before it goes
     into the directory entry. */
  for (i = 0; i < 8; i++)
    base[i] = (char)toupper((unsigned char)base[i]);
  for (i = 0; i < 3; i++)
    ext[i] = (char)toupper((unsigned char)ext[i]);

  if (flex_find_entry(base, ext, &exist_t, &exist_s) == 0)
  {
    prtmsg("File already exists\n");
    return;
  }

  in = fopen((char *)&inbuf[pos], "rb");
  if (in == NULL)
  {
    prtmsg("The file does not exist\n");
    return;
  }

  encoded_len = flex_encode_text(in, flex_iobuf, (int32)sizeof(flex_iobuf));
  fclose(in);
  if (encoded_len < 0)
  {
    prtmsg("File too large\n");
    return;
  }

  sectors_needed = flex_create_file(flex_iobuf, encoded_len, base, ext);
  if (sectors_needed < 0)
    return;

  flex_field_to_str(namebuf, (byte *)base, 8);
  flex_field_to_str(extbuf,  (byte *)ext,  3);
  snprintf(line, sizeof(line), "Saved %s as %s.%s (%d sectors)\n",
           (char *)&inbuf[pos], namebuf, extbuf, sectors_needed);
  prtmsg(line);
}

/*
 *
 * Delete a file from the open FLEX disk image: mark its directory
 * entry deleted and return its sectors to the free chain.
 *
 * The deleted-entry marker is exactly 0xFF (not merely the high bit
 * set) - that's the real FLEX/flexemu convention, and it stays
 * compatible with every existing scan in this file, which already
 * treats any byte with the high bit set as unused.
 *
 * The file's own sector chain is already a valid singly-linked chain
 * in exactly the free-chain's own link format, so returning it is
 * just a splice: append it to the tail of the free chain (tracked by
 * the SIR's fc_end, offset 31-32) by rewriting the current tail
 * sector's own link to point at the deleted file's start, then
 * updating fc_end to the deleted file's end - never prepended, never
 * re-sorted by track/sector, matching flexemu's DeleteCurrent(). If
 * the free chain was empty, the deleted file's chain becomes the
 * whole free chain directly.
 *
 * Syntax: fdelete <name.ext>
 *
 */

void do_fdelete(void)
{
  char  base[8], ext[3];
  char  namebuf[9], extbuf[4];
  char  line[80];
  byte  dirsec[FLEX_SECSZ], sir[FLEX_SECSZ], tailsec[FLEX_SECSZ];
  int   dir_t, dir_s, dir_off, st_t, st_s, en_t, en_s, records;
  int   free_count;

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }
  if (dsk_readonly)
  {
    prtmsg("Disk image is read-only\n");
    return;
  }

  if (!flex_parse_filename(base, ext))
  {
    prtmsg("Usage: fdelete <name.ext>\n");
    return;
  }

  if (flex_find_entry_full(base, ext, &dir_t, &dir_s, &dir_off,
                            &st_t, &st_s, &en_t, &en_s, &records) != 0)
  {
    prtmsg("File not found\n");
    return;
  }

  /* Mark the directory entry deleted first - if anything below fails,
     the file is at worst "deleted but its sectors not yet reclaimed"
     (leaked free space, recoverable later), never a dangling pointer. */
  if (flex_read_sector(dir_t, dir_s, dirsec) != 0)
  {
    prtmsg("Error reading directory sector\n");
    return;
  }
  dirsec[dir_off] = 0xFF;
  if (flex_write_sector(dir_t, dir_s, dirsec) != 0)
  {
    prtmsg("Error writing directory sector\n");
    return;
  }

  if (flex_read_sector(0, 3, sir) != 0)
  {
    prtmsg("Error reading System Information Record\n");
    return;
  }

  if (sir[31] != 0 || sir[32] != 0)
  {
    /* Free chain non-empty: splice onto its tail. */
    if (flex_read_sector(sir[31], sir[32], tailsec) != 0)
    {
      prtmsg("Error reading free-chain tail sector\n");
      return;
    }
    tailsec[0] = (byte)st_t;
    tailsec[1] = (byte)st_s;
    if (flex_write_sector(sir[31], sir[32], tailsec) != 0)
    {
      prtmsg("Error updating free-chain tail sector\n");
      return;
    }
  }
  else
  {
    /* Free chain was empty: the deleted file's chain becomes it. */
    sir[29] = (byte)st_t;
    sir[30] = (byte)st_s;
  }
  sir[31] = (byte)en_t;
  sir[32] = (byte)en_s;

  free_count = sir[33] * 256 + sir[34] + records;
  sir[33] = (byte)(free_count >> 8);
  sir[34] = (byte)(free_count & 0xff);

  if (flex_write_sector(0, 3, sir) != 0)
  {
    prtmsg("Error updating System Information Record\n");
    return;
  }

  flex_field_to_str(namebuf, (byte *)base, 8);
  flex_field_to_str(extbuf,  (byte *)ext,  3);
  snprintf(line, sizeof(line), "Deleted %s.%s (%d sectors freed)\n",
           namebuf, extbuf, records);
  prtmsg(line);
}

/*
 *
 * Rename a file in the open FLEX disk image. Only the 8+3 name/
 * extension bytes of the directory entry are overwritten in place -
 * nothing else (start/end/size/date/attributes) changes, matching
 * flexemu's RenameCurrent(). Refuses if the new name is already in
 * use, matching flexemu's RenameFile() collision guard.
 *
 * Syntax: frename <old name.ext> <new name.ext>
 *
 */

void do_frename(void)
{
  char  oldbase[8], oldext[3], newbase[8], newext[3];
  char  oldnamebuf[9], oldextbuf[4], newnamebuf[9], newextbuf[4];
  char  line[100];
  byte  dirsec[FLEX_SECSZ];
  int   dir_t, dir_s, dir_off, st_t, st_s, en_t, en_s, records;
  int   exist_t, exist_s;

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }
  if (dsk_readonly)
  {
    prtmsg("Disk image is read-only\n");
    return;
  }

  if (!flex_parse_filename(oldbase, oldext))
  {
    prtmsg("Usage: frename <old name.ext> <new name.ext>\n");
    return;
  }
  if (!flex_parse_filename(newbase, newext))
  {
    prtmsg("Usage: frename <old name.ext> <new name.ext>\n");
    return;
  }

  /* inbuf is lowercased by lower() before dispatch, but real FLEX names
     are uppercase-only - upcase the new name/ext before it goes into
     the directory entry. */
  {
    int u;
    for (u = 0; u < 8; u++)
      newbase[u] = (char)toupper((unsigned char)newbase[u]);
    for (u = 0; u < 3; u++)
      newext[u] = (char)toupper((unsigned char)newext[u]);
  }

  if (flex_find_entry_full(oldbase, oldext, &dir_t, &dir_s, &dir_off,
                            &st_t, &st_s, &en_t, &en_s, &records) != 0)
  {
    prtmsg("File not found\n");
    return;
  }

  if (flex_find_entry(newbase, newext, &exist_t, &exist_s) == 0)
  {
    prtmsg("File already exists\n");
    return;
  }

  if (flex_read_sector(dir_t, dir_s, dirsec) != 0)
  {
    prtmsg("Error reading directory sector\n");
    return;
  }
  memcpy(&dirsec[dir_off],     newbase, 8);
  memcpy(&dirsec[dir_off + 8], newext,  3);
  if (flex_write_sector(dir_t, dir_s, dirsec) != 0)
  {
    prtmsg("Error writing directory sector\n");
    return;
  }

  flex_field_to_str(oldnamebuf, (byte *)oldbase, 8);
  flex_field_to_str(oldextbuf,  (byte *)oldext,  3);
  flex_field_to_str(newnamebuf, (byte *)newbase, 8);
  flex_field_to_str(newextbuf,  (byte *)newext,  3);
  snprintf(line, sizeof(line), "Renamed %s.%s to %s.%s\n",
           oldnamebuf, oldextbuf, newnamebuf, newextbuf);
  prtmsg(line);
}

/*
 *
 * Toggle the FLEX disk-write debug trace (see flex_debug / the print
 * in flex_write_sector) - on by default. With no argument, reports
 * the current setting instead of changing it.
 *
 * Syntax: debug [on|off]
 *
 */

void do_debug(void)
{
  char tok[8];
  char line[40];
  int  j, k;

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    snprintf(line, sizeof(line), "Debug mode is %s\n", flex_debug ? "on" : "off");
    prtmsg(line);
    return;
  }

  j = pos;
  k = 0;
  while (inbuf[j] != 0 && inbuf[j] != ' ' && k < (int)sizeof(tok) - 1)
    tok[k++] = inbuf[j++];
  tok[k] = '\0';

  if (strcmp(tok, "on") == 0)
  {
    flex_debug = 1;
    prtmsg("Debug mode on\n");
  }
  else if (strcmp(tok, "off") == 0)
  {
    flex_debug = 0;
    prtmsg("Debug mode off\n");
  }
  else
  {
    prtmsg("Usage: debug on|off\n");
  }
}

/*
 *
 * Identify a sector as a FLEX system sector (boot loader or the SIR)
 * or a directory sector, for sector to report after its dump, and
 * print the fields decoded from it - the same information dskfile or
 * fcat would show, but for just this one sector. Walks the directory
 * chain itself since the caller may be pointed anywhere on the disk.
 * Prints nothing for an ordinary data sector.
 *
 */

static void flex_sector_info(int track, int sector)
{
  byte sir[FLEX_SECSZ];
  byte dirsec[FLEX_SECSZ];
  char fname[9], fext[4];
  char line[80];
  int  t, s, entry, off, count, free_sectors;

  if (dsk_fil == NULL)
    return;

  if (track == 0 && (sector == 1 || sector == 2))
  {
    prtmsg("This is a boot sector.\n");
    return;
  }

  if (track == 0 && sector == 3)
  {
    prtmsg("This is the System Information Record (SIR):\n");
    if (flex_read_sector(track, sector, sir) == 0)
    {
      free_sectors = sir[33] * 256 + sir[34];
      snprintf(line, sizeof(line), "  Volume: %s\n", dsk_label);
      prtmsg(line);
      snprintf(line, sizeof(line), "  Tracks: 0-%d  Sectors/track: %d\n",
               dsk_max_track, dsk_max_sector);
      prtmsg(line);
      snprintf(line, sizeof(line), "  Free sectors: %d\n", free_sectors);
      prtmsg(line);
    }
    return;
  }

  t = 0;
  s = 5;
  while (t != 0 || s != 0)
  {
    if (flex_read_sector(t, s, dirsec) != 0)
      break;
    if (t == track && s == sector)
    {
      count = 0;
      prtmsg("This is a directory sector:\n");
      for (entry = 0; entry < 10; entry++)
      {
        off = 16 + entry * 24;
        if (dirsec[off] == 0 || (dirsec[off] & 0x80))
          continue;
        count++;
        flex_field_to_str(fname, &dirsec[off], 8);
        flex_field_to_str(fext,  &dirsec[off + 8], 3);
        snprintf(line, sizeof(line),
                 "  %-8s %-3s  %5d sectors  %2d/%-2d - %2d/%-2d  %02d/%02d/%04d\n",
                 fname, fext,
                 dirsec[off + 17] * 256 + dirsec[off + 18],
                 dirsec[off + 13], dirsec[off + 14],
                 dirsec[off + 15], dirsec[off + 16],
                 dirsec[off + 21], dirsec[off + 22],
                 flex_year4(dirsec[off + 23]));
        prtmsg(line);
      }
      if (count == 0)
        prtmsg("  (no active entries)\n");
      return;
    }
    t = dirsec[0];
    s = dirsec[1];
  }
}

/*
 *
 * Dump one 256-byte sector of the open disk image as 16 lines of 16
 * hex bytes, followed by their ASCII representation, and the decoded
 * fields if the sector is a recognised FLEX system or directory sector.
 *
 * Syntax: sector [track sector]
 *
 * Called with no arguments, follows the FLEX chain link recorded in
 * bytes 0-1 of the last sector shown (the same link a directory or a
 * file's data sectors are chained with), so repeating "sector" walks
 * the chain the way FLEX itself would - not just the next physical
 * sector on disk.
 *
 */

void do_sector(void)
{
  byte  buf[FLEX_SECSZ];
  int32 track, sector;
  int   row, col;
  char  ch;
  char  line[40];

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    if (dsk_last_track < 0)
    {
      track  = 0;
      sector = 1;
    }
    else if (dsk_last_link_t == 0 && dsk_last_link_s == 0)
    {
      prtmsg("No linked sector (end of chain)\n");
      return;
    }
    else
    {
      track  = dsk_last_link_t;
      sector = dsk_last_link_s;
    }
  }
  else
  {
    track  = getadr();
    sector = getadr();
  }

  if (flex_read_sector((int)track, (int)sector, buf) != 0)
  {
    prtmsg("Invalid track/sector or read error\n");
    return;
  }

  dsk_last_track  = (int)track;
  dsk_last_sector = (int)sector;
  dsk_last_link_t = buf[0];
  dsk_last_link_s = buf[1];

  for (row = 0; row < FLEX_SECSZ; row++)
    memw(row, buf[row]);

  snprintf(line, sizeof(line), "Track %02X Sector %02X\n", (int)track, (int)sector);
  prtmsg(line);

  for (row = 0; row < 16; row++)
  {
    prthex((byte)(row * 16));
    prtmsg("  ");
    for (col = 0; col < 16; col++)
    {
      prthex(buf[row * 16 + col]);
      prtchr(' ');
    }
    prtmsg(" ");
    for (col = 0; col < 16; col++)
    {
      ch = buf[row * 16 + col] & 0x7f;
      if (ch < 32 || ch == 127)
        ch = '.';
      prtchr(ch);
    }
    prtchr('\n');
  }

  flex_sector_info((int)track, (int)sector);
}

/*
 *
 * Write memory 0x00-0xFF back to the open disk image as one 256-byte
 * sector, the reverse of "sector" (which loads a sector's bytes into
 * that same memory range).
 *
 * Syntax: putsector [track sector]
 *
 * Called with no arguments, writes to the track/sector last read or
 * written by "sector"/"putsector" itself, so it can follow the same
 * chain "sector" walked.
 *
 * Always asks for a y/n confirmation naming the target track, sector
 * and disk image before writing - this overwrites disk data with no
 * undo, unlike the other FLEX write commands (flexinject, fdelete,
 * frename, fsave) which only touch sectors implied by the FLEX
 * directory/free-chain structure rather than a raw sector by number.
 *
 */

void do_putsector(void)
{
  byte  buf[FLEX_SECSZ];
  int32 track, sector;
  int   i;
  char  line[40];
  char  confirm[320];
  char  ch;

  if (dsk_fil == NULL)
  {
    prtmsg("No disk image open. Use dskfile <file> first.\n");
    return;
  }
  if (dsk_readonly)
  {
    prtmsg("Disk image is read-only\n");
    return;
  }

  ignblk();
  scancr();
  if (inbuf[pos] == 0)
  {
    if (dsk_last_track < 0)
    {
      prtmsg("No current sector. Use sector <track> <sector> first.\n");
      return;
    }
    track  = dsk_last_track;
    sector = dsk_last_sector;
  }
  else
  {
    track  = getadr();
    sector = getadr();
  }

  snprintf(confirm, sizeof(confirm),
           "Are you sure, writing track %02X sector %02X on %s (y/n)? ",
           (int)track, (int)sector, dsk_name);
  prtmsg(confirm);
  ch = getchr();
  prtchr(ch);
  prtchr('\n');
  if (ch != 'y' && ch != 'Y')
  {
    prtmsg("Aborted.\n");
    return;
  }

  for (i = 0; i < FLEX_SECSZ; i++)
    buf[i] = (byte)memr(i);

  if (flex_write_sector((int)track, (int)sector, buf) != 0)
  {
    prtmsg("Invalid track/sector or write error\n");
    return;
  }

  dsk_last_track  = (int)track;
  dsk_last_sector = (int)sector;
  dsk_last_link_t = buf[0];
  dsk_last_link_s = buf[1];

  snprintf(line, sizeof(line), "Saved Track %02X Sector %02X\n", (int)track, (int)sector);
  prtmsg(line);
}

void do_peps(void )

{
}

void do_reset(void )
{
  ix[0] = ix[1] = ix[2] = 0;
  ix[3] = 0x7000;
  accu[0] = accu[1] = accu[2] = accu[3] = 0;
  pc = mem[0xFFFE] * 0x100 + mem[0xFFFF];
  do_reg();
}

/*
 * help [topic] - with no argument, prints the full command reference
 * unchanged. With a recognised topic word (s1, flex, 6809), prints
 * only that slice instead - these are curated cross-cuts, not new
 * categories: "s1" in particular pulls together the S-record commands
 * that are otherwise split across the File and Serial terminal
 * sections below, and "6809" is the assembler+disassembler+simulator
 * commands without the general line-editing reference.
 */

void do_help(void )

{
  char topic[16];
  int  i;

  ignblk();
  scancr();
  i = 0;
  while (inbuf[pos] != 0 && inbuf[pos] != ' ' && i < (int)sizeof(topic) - 1)
    topic[i++] = inbuf[pos++];
  topic[i] = '\0';

  if (strcmp(topic, "s1") == 0)
  {
    prtmsg("S1 (Motorola S-record) commands:\n");
    prtmsg("  s1load <file>                - load Motorola S1 record file\n");
    prtmsg("  s1send <start> <end> [name]  - send memory as S1 to stdout\n");
    prtmsg("  s1save <start> <end> <file>  - save memory as an S1 record file\n");
    prtmsg("  s1port <start> <end> [baud]  - send memory as bare S1 records (no S0/S9) to a USB-serial device\n");
    prtmsg("    (1ms delay between characters, sends 'q' at the end and shows the board's reply)\n");
    prtmsg("  baud rates (s1port/sendfile/term): 2400 9600 (default) 19200 28800 38400 76800 153600\n");
    return;
  }

  if (strcmp(topic, "flex") == 0)
  {
    prtmsg("FLEX 6809 disk image commands:\n");
    prtmsg("  dskfile [file.dsk]           - open a FLEX disk image, or show the current one\n");
    prtmsg("  fcat                         - list directory of open image\n");
    prtmsg("  flist  <name.ext>            - list a FLEX text file as text\n");
    prtmsg("  fget   <name.ext> [addr]     - load a FLEX file into memory\n");
    prtmsg("  flex2file <name.ext> <hostfile> - copy a FLEX text file to Linux\n");
    prtmsg("  fsave  <start> <end> <name.ext> - save memory as a new FLEX file\n");
    prtmsg("  flexinject <file>            - import a host text file as a new FLEX file\n");
    prtmsg("    (FLEX name/ext derived from the host filename, e.g. hello.txt -> HELLO.TXT)\n");
    prtmsg("  fdelete <name.ext>           - delete a file from the open image\n");
    prtmsg("  frename <old.ext> <new.ext>  - rename a file in the open image\n");
    prtmsg("  sector [track sector]       - dump one disk sector, or the next one (also\n");
    prtmsg("    copies it into mem 0x00-0xFF, paired with putsector below)\n");
    prtmsg("  putsector [track sector]    - write memory 0x00-0xFF back as a sector (asks y/n)\n");
  prtmsg("  debug [on|off]               - trace every FLEX disk write (track/sector + first 4 bytes)\n");
  prtmsg("    (on by default; no argument reports the current setting)\n");
    return;
  }

  if (strcmp(topic, "6809") == 0)
  {
    prtmsg("Disassembler / assembler:\n");
    prtmsg("  dis    <addr>                - disassemble from address\n");
    prtmsg("    (Space = next instruction, any other key = stop)\n");
    prtmsg("  ass    <addr>                - one-line assembler ($ for hex)\n");
    prtmsg("    (enter instruction, - goes back, + goes forward, * to stop)\n");
    prtmsg("\n");
    prtmsg("6809 simulator:\n");
    prtmsg("  reg                          - show all registers\n");
    prtmsg("  set    <r> <val>             - set register (one char: p x y u s a b d c e f w)\n");
    prtmsg("    (pc/cc alias p/c; e/f/w are the 6309 E/F/W regs)  e.g. set pc e000  set a ff\n");
    prtmsg("  reset                        - reset regs as 6809 cold (PC=[FFFE])\n");
    prtmsg("  run    [addr]                - run from current PC, or from [addr] if given\n");
    prtmsg("    (prints \"Running from XXXX\" when [addr] is given; ESC breaks a running program)\n");
    prtmsg("  mode   <options>             - set simulator options (mode h for help)\n");
    prtmsg("    s=single-step  r=show-regs  i=show-instr\n");
    prtmsg("    p <addr>=break-at-PC  q <addr>=2nd PC breakpoint  g <n>=run-n-instructions\n");
    prtmsg("\n");
    prtmsg("6309 extras (a few opcodes only, not a full 6309):\n");
    prtmsg("  divd, divq                   - signed 16/8-bit, 32/16-bit divide (asm/dis/sim)\n");
    prtmsg("  save, load (asm mnemonics)   - CPU-level host file I/O, opcodes $18/$15:\n");
    prtmsg("    X=filename (dot ok, ends at first byte < $21), Y/U=start/end for save,\n");
    prtmsg("    Y=start for load (Y ends at last byte read) - not the monitor commands above\n");
    prtmsg("  prtacc, getacc, break, delay1ms, sensekey (asm mnemonics) - other host I/O ops\n");
    return;
  }

  if (topic[0] != 0)
  {
    prtmsg("Unknown help topic. Try: help s1 | help flex | help 6809\n");
    return;
  }

  prtmsg("Commands (all can be abbreviated, e.g. l.1000 = list 1000):\n");
  prtmsg("Topics: help s1 | help flex | help 6809\n");
  prtmsg("\n");
  prtmsg("Memory commands:\n");
  prtmsg("  list   <start> [end] [width] - hex+ASCII dump of memory\n");
  prtmsg("    (omit [end] to page one line at a time - any key = next line, CR = stop)\n");
  prtmsg("  fill   <start> <end> <byte>  - fill memory range with byte\n");
  prtmsg("  move   <start> <end> <to>    - copy memory block\n");
  prtmsg("  modify <addr>  <byte>        - write one byte\n");
  prtmsg("  check  <start> <end>         - compute checksum of range\n");
  prtmsg("  verify <start> <end> <addr2> - compare two memory regions\n");
  prtmsg("  find   <pattern@addr> <start>- search memory for byte pattern\n");
  prtmsg("    (pattern: first byte = count, then the bytes to match)\n");
  prtmsg("\n");
  prtmsg("File commands:\n");
  prtmsg("  load   <addr> <file>         - load raw binary file to address\n");
  prtmsg("  save   <start> <end> <file>  - save raw binary to file\n");
  prtmsg("  s1load <file>                - load Motorola S1 record file\n");
  prtmsg("  s1send <start> <end> [name]  - send memory as S1 to stdout\n");
  prtmsg("  s1save <start> <end> <file>  - save memory as an S1 record file\n");
  prtmsg("  exec   <file>                - run a file of mgmon commands, one per line\n");
  prtmsg("    (blank lines and lines starting with # ; or * are comments)\n");
  prtmsg("\n");
  prtmsg("FLEX 6809 disk image commands:\n");
  prtmsg("  dskfile [file.dsk]           - open a FLEX disk image, or show the current one\n");
  prtmsg("  fcat                         - list directory of open image\n");
  prtmsg("  flist  <name.ext>            - list a FLEX text file as text\n");
  prtmsg("  fget   <name.ext> [addr]     - load a FLEX file into memory\n");
  prtmsg("  flex2file <name.ext> <hostfile> - copy a FLEX text file to Linux\n");
  prtmsg("  fsave  <start> <end> <name.ext> - save memory as a new FLEX file\n");
  prtmsg("  flexinject <file>            - import a host text file as a new FLEX file\n");
  prtmsg("    (FLEX name/ext derived from the host filename, e.g. hello.txt -> HELLO.TXT)\n");
  prtmsg("  fdelete <name.ext>           - delete a file from the open image\n");
  prtmsg("  frename <old.ext> <new.ext>  - rename a file in the open image\n");
  prtmsg("  sector [track sector]       - dump one disk sector, or the next one (also\n");
  prtmsg("    copies it into mem 0x00-0xFF, paired with putsector below)\n");
  prtmsg("  putsector [track sector]    - write memory 0x00-0xFF back as a sector (asks y/n)\n");
  prtmsg("  debug [on|off]               - trace every FLEX disk write (track/sector + first 4 bytes)\n");
  prtmsg("    (on by default; no argument reports the current setting)\n");
  prtmsg("\n");
  prtmsg("Disassembler / assembler:\n");
  prtmsg("  dis    <addr>                - disassemble from address\n");
  prtmsg("    (Space = next instruction, any other key = stop)\n");
  prtmsg("\nLine editing (Emacs keys + arrow keys):\n");
  prtmsg("  Up/Ctrl-P  previous cmd   Down/Ctrl-N  next cmd\n");
  prtmsg("  Left/Ctrl-B  back char    Right/Ctrl-F  forward char\n");
  prtmsg("  Home/Ctrl-A  line start   End/Ctrl-E    line end\n");
  prtmsg("  Ctrl-K  kill to end       Backspace/DEL delete char\n");
  prtmsg("  Ctrl-R  reverse search history (type to search, Enter to run)\n");
  prtmsg("  ass    <addr>                - one-line assembler ($ for hex)\n");
  prtmsg("    (enter instruction, - goes back, + goes forward, * to stop)\n");
  prtmsg("\n");
  prtmsg("6809 simulator:\n");
  prtmsg("  reg                          - show all registers\n");
  prtmsg("  set    <r> <val>             - set register (one char: p x y u s a b d c e f w)\n");
  prtmsg("    (pc/cc alias p/c; e/f/w are the 6309 E/F/W regs)  e.g. set pc e000  set a ff\n");
  prtmsg("  reset                        - reset regs as 6809 cold (PC=[FFFE])\n");
  prtmsg("  run    [addr]                - run from current PC, or from [addr] if given\n");
  prtmsg("    (prints \"Running from XXXX\" when [addr] is given; ESC breaks a running program)\n");
  prtmsg("  mode   <options>             - set simulator options (mode h for help)\n");
  prtmsg("    s=single-step  r=show-regs  i=show-instr\n");
  prtmsg("    p <addr>=break-at-PC  q <addr>=2nd PC breakpoint  g <n>=run-n-instructions\n");
  prtmsg("\n");
  prtmsg("6309 extras (a few opcodes only, not a full 6309):\n");
  prtmsg("  divd, divq                   - signed 16/8-bit, 32/16-bit divide (asm/dis/sim)\n");
  prtmsg("  save, load (asm mnemonics)   - CPU-level host file I/O, opcodes $18/$15:\n");
  prtmsg("    X=filename (dot ok, ends at first byte < $21), Y/U=start/end for save,\n");
  prtmsg("    Y=start for load (Y ends at last byte read) - not the monitor commands above\n");
  prtmsg("  prtacc, getacc, break, delay1ms, sensekey (asm mnemonics) - other host I/O ops\n");
  prtmsg("\n");
  prtmsg("Program info:\n");
  prtmsg("  info                         - list all available commands\n");
  prtmsg("  program <addr>               - interactive byte editor (old EPROM-programmer UI)\n");
  prtmsg("    (+/- move, hex bytes to write, \"text\" writes a string, . to exit)\n");
  prtmsg("  peps                         - PEPS programmer interface (stub, not implemented)\n");
  prtmsg("  sys    <shell command>        - execute a Mac/Linux shell command\n");
  prtmsg("  exit                         - quit mgmon\n");
  prtmsg("\n");
  prtmsg("Serial terminal:\n");
  prtmsg("  baud rates: 2400 9600 (default) 19200 28800 38400 76800 153600\n");
  prtmsg("  term [baud] [break-byte-hex] - connect to a USB-serial device, 8N1\n");
  prtmsg("    lists /dev/cu.usbserial-*/cu.usbmodem* devices to pick from, ESC to return\n");
  prtmsg("    (default break is ESC; e.g. term 03 breaks on Ctrl-C instead, at default baud)\n");
  prtmsg("  s1port <start> <end> [baud]  - send memory as bare S1 records (no S0/S9) to a USB-serial device\n");
  prtmsg("    (1ms delay between characters, sends 'q' at the end and shows the board's reply)\n");
  prtmsg("  sendfile <file> [baud]       - copy a file from this machine straight to a USB-serial device\n");
  prtmsg("    (raw bytes, no S-record framing, 1ms delay between characters, sends 'q' and shows the board's reply)\n");
  prtmsg("\n");
  prtmsg("Note: all addresses and values in hex. End address is exclusive.\n");
  prtmsg("Known limitations: SWI/SWI2/SWI3, RTI, CWAI and SYNC are not implemented.\n");
  prtmsg("\nGood luck !\n");
}

void do_asm(void )

{
    int i, ln;

    static unint16 pc;

    pc = getadr();
    do
    {
        if (!mode_sim)
           printf("%04X %02X ",pc,mem[pc]);
        buffer();
        if ((inbuf[0] == '-') || (inbuf[0] == '+'))
        {
            if (inbuf[0] == '-')
                pc--;
            else
                pc++;
            ln = 1;
        }
        else
        {
            do_ass(stdout, MEMORY, pc);
            if (mode_i)
                printf("%04X:",pc);
            ln = inbuf[0];
            for (i = 1; i <= ln; i++)
            {
               if (mode_i)
                  printf("%02X ", inbuf[i + 3]);
               mem[pc] = inbuf[i + 3];
               pc++;
            }
            if (mode_i)
                prtmsg("\n");
        }
    }
    while (ln != 0);
}
