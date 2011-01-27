/* nc100em, an Amstrad NC100 emulator.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996,1999 Russell Marks.
 * Copyright (C) 2009 by Nilquader of SPRING!
 *
 * fdc.h - NEC765 emulation
 */

// Definitions
#define DISKSIZE 737280 // 2 sided, 80 tracks, 9 sectors, 512 bytes per sector = 720K

// Main Status Register constants
#define MAIN_D0B 0x01 // Drive 0 Busy
#define MAIN_D1B 0x02 // Drive 1 Busy
#define MAIN_D2B 0x04 // Drive 2 Busy
#define MAIN_D3B 0x08 // Drive 3 Busy
#define MAIN_CB  0x10 // FDC Busy
#define MAIN_EXM 0x20 // Execution mode
#define MAIN_DIO 0x40 // Data In/Out (DIO=1: FDC->CPU; DIO=0: CPU->FDC)
#define MAIN_RQM 0x80 // Request for Master (Ready to receive/send data)

// 0th Status Register constants
#define S0_US0 0x01 // Unit Select 0
#define S0_US1 0x02 // Unit Select 1
#define S0_HD 0x04 // Head Address
#define S0_NR 0x08 // Not Ready
#define S0_EC 0x10 // Equipment Check
#define S0_SE 0x20 // Seek End
#define S0_ICL 0x40 // Interrupt Code (LSB)
#define S0_ICH 0x80 // Interrupt Code (MSB)

// 3rd Status Register constants
#define S3_US0 0x01 // Unit Select 0
#define S3_US1 0x02 // Unit Select 1
#define S3_HD 0x04 // Head Adress
#define S3_TS 0x08 // Two Side
#define S3_T0 0x10 // Track 0
#define S3_RY 0x20 // Ready
#define S3_WP 0x40 // Write Protected
#define S3_FT 0x80 // Fault

// FDC Commands
#define CMD_READDATA 0x06
#define CMD_READDELETEDDATA 0x0C
#define CMD_WRITEDATA 0x05
#define CMD_WRITEDELETEDDATA 0x09
#define CMD_READDIAGNOSTIC 0x02
#define CMD_READID 0x0A
#define CMD_WRITEID 0x0D
#define CMD_SCANEQUAL 0x11
#define CMD_SCANLOWEQUAL 0x19
#define CMD_SCANHIGHEQUAL 0x1D
#define CMD_RECALIBRATE 0x07
#define CMD_SENSEINT 0x08
#define CMD_SPECIFY 0x03
#define CMD_SENSEDRV 0x04
#define CMD_VERSION 0x10
#define CMD_SEEK 0x0F

// Phase status constants
#define P_COMMAND 0
#define P_EXECUTION 1
#define P_RESULT 2

/* Communication with host */
unsigned char fdc_getmainstatus(void);
unsigned char fdc_read(void);
void fdc_write(unsigned char a);
void fdc_terminalcount(void);
int fdc_interrupt(int clear);
int fdc_insertdisc(void);

/* IMPLEMENTATION OF FDC COMMANDS */
void fdc_read_data(void);
void fdc_read_id(void);
void fdc_senseint(void);
void fdc_seek(int track);

/* INTERNAL SUBROUTINES */
void fdc_setresult(int track, int head, int sector);
unsigned char fdc_sendbyte(void);

/* end */
