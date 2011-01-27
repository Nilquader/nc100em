/* nc100em, an Amstrad NC100 emulator.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996,1999 Russell Marks.
 * Copyright (C) 2009 by Nilquader of SPRING!
 *
 * fdc.c - NEC765 emulation
 */
 
#include "fdc.h" 
#include <stdio.h>
#include <stdlib.h>

#undef FDC_DEBUG

// Global variables
unsigned char fdc_status_main = MAIN_RQM; // main status register
int fdc_interrupt_pending = 0; // Need to process an FDC Interrupt?
int fdc_track; // current track on disc
int fdc_sector; // current sector on disc
int fdc_bytepos; // current byte position on sector
unsigned char *floppydata = NULL; // Pointer to floppy contents

// FDC State machine
int phase = P_COMMAND;
int CmdByte = 0; // number of command byte received
int ResByte = 0; // number of result byte sent
int resultlen = 0; // length of result data
unsigned char fdccommand[9]; // complete FDC command
unsigned char fdcresult[7]; // complete FDC result code
unsigned char fdc_status[] = {0,0,0,S3_TS|S3_T0|S3_RY}; // FDC status registers

unsigned char fdc_getmainstatus(void) { // return main status register
	
	if (!(fdc_status_main&MAIN_RQM)) { // RQM not set
		fdc_status_main|=MAIN_RQM; // set RQM, so that the controller will be ready next time *EXPERIMENTAL CODE*
	}

#ifdef FDC_DEBUG
	printf("Main Status Register read, 0x%02x\n", fdc_status_main);
#endif

	return fdc_status_main;	
}

unsigned char fdc_read(void) { // Read byte from data register
	/* variable declaration */
	unsigned char output = 0; // return value

	switch(phase) {
		case P_EXECUTION: // EXECUTION phase
			output = fdc_sendbyte(); // send byte from disc to CPU
			break;
		case P_RESULT: // RESULT phase
			output = fdcresult[ResByte];
			ResByte++;
			if (ResByte == resultlen) { // RESULT phase finished
				phase = P_COMMAND; // return to COMMAND phase
				fdc_status_main&=~MAIN_DIO; // reset data direction flag to Input (CPU->FDC)
				fdc_status_main&=~MAIN_CB; // Clear CB (busy) flag
			}
			break;
	}
	
#ifdef FDC_DEBUG
	printf("FDC DATA READ, 0x%02x\n", output);
#endif	
	
	return output;
}

void fdc_write(unsigned char a) { // Write byte to data register
	static int cmdlen = 9; // length of FDC command
#ifdef FDC_DEBUG	
	printf("FDC DATA WRITE = 0x%x\n", a);
#endif	
	switch(phase) {
		case P_COMMAND:
			fdccommand[CmdByte] = a; // store part of command
			if (CmdByte == 0) { // first byte of command
				switch(a&0x1F) {
					case CMD_READDATA:
					case CMD_READDELETEDDATA:
					case CMD_WRITEDATA:
					case CMD_WRITEDELETEDDATA:
					case CMD_READDIAGNOSTIC:
					case CMD_SCANEQUAL:
					case CMD_SCANLOWEQUAL:
					case CMD_SCANHIGHEQUAL:
						cmdlen = 9;
						break;
					case CMD_WRITEID:
						cmdlen = 6;
						break;
					case CMD_SPECIFY:
					case CMD_SEEK:
						cmdlen = 3;
						break;
					case CMD_READID:
					case CMD_RECALIBRATE:
					case CMD_SENSEDRV:
						cmdlen = 2;
						break;
					case CMD_SENSEINT:
					case CMD_VERSION:
						cmdlen = 1;
						break;
					default: // Invalid Command
						cmdlen = 1;
						printf("Invalid FDC Command received: 0x%x\n", a);
						break;	
				}
			// Set CB (Busy) flag
			//fdc_status_main|=MAIN_CB;	
			}
			CmdByte++;
			if (CmdByte == cmdlen) { // Command phase finished
				switch(fdccommand[0]&0x1F) {
					case CMD_READDATA:
						fdc_read_data();
						break;
					case CMD_READDELETEDDATA:
						printf("*** Unimplemented FDC Command READ DELETED DATA\n");
						break;
					case CMD_WRITEDATA:
						printf("*** Unimplemented FDC Command WRITE DATA\n");
						break;
					case CMD_WRITEDELETEDDATA:
						printf("*** Unimplemented FDC Command WRITE DELETED DATA\n");
						break;
					case CMD_READDIAGNOSTIC:
						printf("*** Unimplemented FDC Command READ DIAGNOSTIC\n");
						break;
					case CMD_READID:
						// Read sector ID
						fdc_read_id();
						break;
					case CMD_WRITEID:
						printf("*** Unimplemented FDC Command WRITE ID\n");
						break;
					case CMD_SCANEQUAL:
						printf("*** Unimplemented FDC Command SCAN EQUAL\n");
						break;
					case CMD_SCANLOWEQUAL:
						printf("*** Unimplemented FDC Command SCAN LOW OR EQUAL\n");
						break;
					case CMD_SCANHIGHEQUAL:
						printf("*** Unimplemented FDC Command SCAN HIGH OR EQUAL\n");
						break;
					case CMD_RECALIBRATE:
						// Reset to track 0
						fdc_seek(0);
						break;
					case CMD_SENSEINT:
						// Get Interrupt status register
						fdc_senseint();
						break;
					case CMD_SPECIFY:
						// We really don't need to do anything here. This command only sets
						// physical timing parameters. These are not relevant to a virtual FDC.
						fdc_status_main&=~MAIN_CB; // Clear CB (busy) flag
						break;
					case CMD_SENSEDRV:
						printf("*** Unimplemented FDC Command SENSE DRIVE\n");
						break;
					case CMD_VERSION:
						printf("*** Unimplemented FDC Command VERSION\n");
						break;
					case CMD_SEEK:
						// Seek to a specified track
						fdc_seek(fdccommand[2]);
						break;
					default:
						break;
				}
				CmdByte = 0;
			}
			break;
		case P_EXECUTION: // Execution phase
			break;
		case P_RESULT: // There should be no write acces in result phase
			break;
	}
}

void fdc_terminalcount(void) { // Terminal Count
	printf("*** TC set\n");
	if (phase == P_EXECUTION) { // if in EXECUTION phase
		phase = P_RESULT; // stop it and enter RESULT phase
		fdc_status_main&=~MAIN_EXM; // clear EXM (execution mode) flag
		fdc_interrupt_pending = 1; // do interrupt, beacause of leaving EXECUTION mode
	}
}

int fdc_interrupt(int clear) { // Ask for pending int
	if(fdc_interrupt_pending) {
		if (clear) fdc_interrupt_pending = 0;
		return(1);
	} else return (0);
}

int fdc_insertdisc(void) { // Inserts a disc into virtual drive
	FILE *fileptr;
	long i = 0;
	char *filename = "floppy.img"; 
	
	if(floppydata != NULL)
		return 0; // ERROR - already inserted;
		
	// Allocate memory for floppy image
	floppydata=malloc(DISKSIZE*sizeof(unsigned char));
	if (floppydata == NULL)
		return 0; // ERROR while allocating memory
	
	// read file into buffer
	if ((fileptr=fopen(filename,"rb"))==NULL) // open file
		return 0; // ERROR while opening file
		
	while((!feof(fileptr))&&(i<DISKSIZE)) {
		floppydata[i] = getc(fileptr);
		i++;
	}
	
	printf("Reserved bytes: %i, Read last: %i\n",DISKSIZE*sizeof(unsigned char),i-1);
	
	// close file
	fclose(fileptr);
	printf("Disc opened successfull.\n");
	return 1;
}

/* IMPLEMENTATION OF FDC COMMANDS */

/* Read Data */
void fdc_read_data(void) {
	printf("Reading Sector %i at Track %i, Head %i\n", fdccommand[4], fdc_track, fdccommand[3]);
	
	// gather data for result phase
	fdc_setresult(fdc_track, fdccommand[3], fdccommand[4]);
	
	// enqueue interrupt
	fdc_interrupt_pending = 1;
	
	// enter EXECUTION phase
	fdc_sector=fdccommand[4]; // set sector number to read
	fdc_bytepos=0; // set byte position to beginning of sector
	fdc_status_main|=MAIN_DIO; // set data direction Flag to Output (FDC -> CPU)
	fdc_status_main|=MAIN_EXM; // set EXM flag to announce EXECUTION mode to the CPU
	phase = P_EXECUTION; // advance to EXECUTION state
	
	// fdc_status_main&=~MAIN_RQM; // **EXPERIMENTAL** Clear Ready-FLag, cpu must wait a little for the data
	// fdc_status_main = 0xF0; // ** EXPERIMENTAL, also set CB /* RQM=1, DIO=FDC->CPU, EXM=1, CB=1 */
}

/* Read Sector ID */
void fdc_read_id(void) {
	int head = (fdccommand[1]&0x04)>>2;
	
	printf("Reading Sector ID at Track %i, Head %i\n", fdc_track, fdccommand[1]);
	
	// gather result data to send to CPU
	fdc_setresult(fdc_track, head, 1);
	
	// enqueue interrupt
	fdc_interrupt_pending = 1;
	
	// enter RESULT phase
	fdc_status_main|=MAIN_DIO; // set data direction Flag to Output (FDC -> CPU)
	phase = P_RESULT; // advance to RESULT state
} /* end of fdc_read_id() */

/* Get interrupt status register (R0) */
void fdc_senseint(void) {
	// gather data to send to CPU
	fdcresult[0] = fdc_status[0];
	fdcresult[1] = fdc_track;
	resultlen = 2;
	ResByte = 0;
	
	// clear 'DRIVE x BUSY' flags in main status register
	fdc_status_main&=~(MAIN_D0B|MAIN_D1B|MAIN_D2B|MAIN_D3B);
	
	// clear Interrupt Status register
	fdc_status[0] = 0;
	
	// enter RESULT phase
	fdc_status_main|=MAIN_DIO; // set data direction Flag to Output (FDC -> CPU)
	phase = P_RESULT; // advance to RESULT state
} /* end of fdc_senseint() */

/* Seek to a specified track */
void fdc_seek(int track) {
	int head = (fdccommand[1]&0x04)>>2;
	
	// Only one drive supported, so there is only one 'track' variable
	printf("Seeking to Track %i\n", track);
	fdc_track = track;
	if (track == 0) 
		fdc_status[3]|=S3_T0; // Set 'Track 0' flag if track is 0
	else
		fdc_status[3]&=~S3_T0; // Clear 'Track 0' flag if track is not 0
	if (head == 0)
		fdc_status[0]&=~S0_HD; // Set Head to '0'
	else
		fdc_status[0]|=S0_HD; // Set Head to '1' 
	fdc_status[0]|=S0_SE; // Set 'Seek End' flag
	
	switch(fdccommand[1]&0x03) { // Drive Select, set BUSY Flag
		case 0:
			fdc_status_main|=MAIN_D0B; break;
		case 1:
			fdc_status_main|=MAIN_D1B; break;
		case 2:
			fdc_status_main|=MAIN_D2B; break;
		case 3:
			fdc_status_main|=MAIN_D3B; break;
	}
	fdc_interrupt_pending = 1;
} /* end of fdc_seek() */

/* INTERNAL SUBROUTINES */

/* set the standard 7-byte result data */
void fdc_setresult(int track, int head, int sector) {
	// gather data to send to CPU
	fdcresult[0] = fdc_status[0];
	fdcresult[1] = fdc_status[1];
	fdcresult[2] = fdc_status[2];
	fdcresult[3] = track; // C
	fdcresult[4] = head; // H
	fdcresult[5] = sector; // R (Sector number)
	fdcresult[6] = 2; // N (Sector size, log2(512)-7=2)
	resultlen = 7;
	ResByte = 0;

	printf("Setting command result: R0:%02X R1:%02X R2:%02X TRACK:%i HEAD:%i SECTOR:%i\n", fdc_status[0], fdc_status[1], fdc_status[2], track, head, sector);
}

unsigned char fdc_sendbyte(void) { // Send byte form disc to CPU
	int head = (fdccommand[1]&0x04)>>2;
	int lba; // LBA address of sector
	unsigned char output;
		
	if (!(fdc_status_main&MAIN_RQM)) return 0; // *EXPERIMENTAL* We're not ready yet!
	
	// Calculate byte postiton
	// Wikipedia: LBA = ( ( CYL * HPC + HEAD ) * SPT ) + SECT - 1
	lba = (( fdc_track * 2 + head) * 9) + fdc_sector - 1;
	output = floppydata[(lba*512) + fdc_bytepos];
	fdc_bytepos++;
	if (fdc_bytepos == 512) { // end of sector
		fdc_bytepos = 0;
		fdc_sector++;
		if(fdc_sector > 9) { // end of track
			phase = P_RESULT;
			fdc_status_main&=~MAIN_EXM; // clear EXM (execution mode) flag
		}
	}
	
	// new interrupt for next byte (or for end of EXECUTION phase, it's needed anyway)
	// fdc_interrupt_pending = 1;
	
	return output;
}
