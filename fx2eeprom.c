/*

    Read and Write the EEPROM of an FX2 chip with the help of the Cypress vend_ax firmware.

    - Read example: read SIZE bytes from USB device with VID:PID
      ./fx2eeprom r VID PID SIZE > eeprom.raw
    - Write example: write SIZE bytes to USB device with VID:PID
      ./fx2eeprom w VID PID SIZE < eeprom.raw

    SIZE must not be greater than 65536.
    You can omit the SIZE parameter, in which case it defaults to 65536.
    In read mode, the tool then outputs SIZE bytes to stdout;
    in write mode, it stores up to SIZE bytes from stdin (until EOF).

    An additional ADDRESS parameter can be appended (default = 0)
    to start the read / write at this EEPROM address.

    - Read example: read 256 bytes from VID:PID starting at EEPROM addr 1024
      ./fx2eeprom r VID PID 256 1024 > eeprom_256_at_1024.raw

    Copyright Ricardo Ribalda - 2012 - ricardo.ribalda@gmail.com
    Copyright Martin Homuth-Rosemann - 2023

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include "vend_ax.h"

#define TRANS_TYPE_READ 0xc0
#define TRANS_TYPE_WRITE 0x40
#define EEPROM 0xa2
#define TIMEOUT 1000
#define LOCATION (1<<4)
#define CHUNKSIZE 64

enum {READ,WRITE};


static void use(char *prog){
	fprintf(stderr,"%s w/r VID PID [ size [ee_addr] ]\n",prog);
	return;
}


int main(int argc, char *argv[]){
	struct libusb_device_handle *dev;
	int ret;
	unsigned char buffer[CHUNKSIZE];
	int pid, vid, length, xfer_len;
	int address=0;
	int r_count=0, w_count=0;
	int mode;

	if (argc<4){
		use(argv[0]);
		return -1;
	}

	if (argc>4) /* explicite size */
		length=strtoul(argv[4],NULL,0);
	else
		length=0x10000; /* max transfer size */
	if (argc>5) /* EEPROM address */
		address=strtoul(argv[5],NULL,0);
	else
		address=0; /* start from bottom */

	if ((argv[1][0]=='W')||(argv[1][0]=='w'))
		mode=WRITE;
        else
		mode=READ;

	vid=strtoul(argv[2],NULL,0);
	pid=strtoul(argv[3],NULL,0);


	ret=libusb_init(NULL);
	if (ret<0){
		fprintf(stderr,"Unable to init libusb\n");
		perror("libusb_init");
		return -1;
	}

	dev=libusb_open_device_with_vid_pid(NULL,vid,pid);
	if (!dev){
		fprintf(stderr,"Unable to find device\n");
		perror("libusb_open");
		return -1;
	}

	if (libusb_kernel_driver_active(dev, 0) && libusb_detach_kernel_driver(dev, 0)){
			fprintf(stderr,"Unable to detach kernel driver\n");
			perror("libusb_detach_kernel_driver");
			return -1;
	}

	ret=libusb_claim_interface(dev,0);
	if (ret<0){
		fprintf(stderr,"Unable to clain interface\n");
		perror("libusb_claim_interface");
		return -1;
	}

	/* Load the vend_ax firmware that supports the EEPROM read/write commands. */
	ret = fx2_load_vendax(dev);
	if ( ret != 0 ) {
		fprintf(stderr, "Error: Failed to load vend_vx firmware\n");
		return -4;
	}

	while ( length ) { /* read/write chunks of max CHUNKSIZE byte */
		if ( length > CHUNKSIZE ) {
			xfer_len = CHUNKSIZE;
			length -= CHUNKSIZE;
		} else {
			xfer_len = length;
			length = 0;
		}

		if (mode==READ){
			ret=libusb_control_transfer(dev,TRANS_TYPE_READ,EEPROM,address,
						    LOCATION,buffer,xfer_len,TIMEOUT);
			if ( ret < 0 ) {
				fprintf(stderr,"Unable to control transfer\n");
				perror("libusb_control_transfer");
				return -1;
			}
			address += CHUNKSIZE;
			r_count += ret;
			w_count += fwrite(buffer,1,ret,stdout);
		} else { /* WRITE */
			ret = fread(buffer,1,xfer_len,stdin);
			if (ret < xfer_len){ /* too few bytes? */
				if (argc > 4) { /* we want an exact number of bytes */
					fprintf(stderr,"Wrong size from stdin - expected %d, got %d\n",xfer_len,r_count);
					perror("fread");
					return -1;
				} else {
					if (ret == 0) /* EOF */
						break; /* ready */
					xfer_len = ret; /* use only the available number of bytes */
				}
			}
			r_count += ret;

			ret = libusb_control_transfer(dev,TRANS_TYPE_WRITE,EEPROM,address,
						      LOCATION,buffer,xfer_len,TIMEOUT);
			if (ret<0){
				fprintf(stderr,"Unable to control transfer\n");
				perror("libusb_control_transfer");
				return -1;
			}
			address += CHUNKSIZE;
			w_count += ret;
		}
	}

	if (mode == READ) {
		fprintf(stderr,"Read %d bytes from EEPROM\n",r_count);
		fprintf(stderr,"Wrote %d bytes to stdout\n",w_count);
	} else {
		fprintf(stderr,"Read %d bytes from stdin\n",r_count);
		fprintf(stderr,"Wrote %d bytes to EEPROM\n",w_count);
	}

	libusb_close(dev);
	libusb_exit(NULL);

	return 0;
}
