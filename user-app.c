/*
 ============================================================================
 Name        : ESP-Lab3.c
 Author      : Rohit Khanna
 Version     :
 Copyright   : Your copyright notice
 Description : user-app for ESP Lab3

  	15 address space = 32KB, Page size = 64 B therefore we have 32KB/64B pages = 2^9 = 512 pages
  	 global pointer to maintain the current page position of EEPROM where it can write.
  	 Will vary from 0 to 511

 ============================================================================
 */


#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#define I2C_SLAVE	0x0703	/* Use this slave address  for ioctl*/

int FILE_DESC = 0;
uint PAGE_SIZE = 64;
uint8_t HIGHER_BYTE = 0;
uint8_t LOWER_BYTE = 0;
int EEPROM_ADDR = 0x52;         // The I2C address of the EEPROM
int ADAPTER_NO = 2; 			/*	inspect /sys/class/i2c-dev/ or run "i2cdetect -l" to decide this.	*/
int SLEEP_INTERVAL = 10000;			// sleep after every read and write for SLEEP_INTERVAL seconds, .01 ms


/*
 * updates the global address maintained by LOWER_BYTE and HIGHER_BYTE by incrementing by 64 ie PAGE SIZE
 * and wraps around if its the last page ie 32704
 */
void updateAddress(){
	uint address = LOWER_BYTE + (HIGHER_BYTE<<8);
	if (address >= 32704)
		//last page starts from addr 32704 to 32767
		address = 0;
	else
		address += 64;

	LOWER_BYTE = address & 255;
	HIGHER_BYTE = address >> 8;
}



/*
 * int seek_EEPROM(int offset): to set the current page position in the EEPROM to offset which is
 * the page number in the range of 0 to 2k-1. Return 0 if offset is in the address range of the EEPROM, or
 * –1 otherwise.
 */
int seek_EEPROM(int offset){				//offset is page number
	int page_number = offset;
	printf("seek_EEPROM()\n");
	int res;
	char *buffer = malloc(sizeof(char)*2);
	int address;

	if(page_number<0 || page_number>511){										// total PAGE COUNT = 512 (0 to 511)
		printf("Invalid offset or page number\n");
		return -1;
	}

	address = page_number*PAGE_SIZE;
	LOWER_BYTE = address & 255;
	HIGHER_BYTE = address >> 8;

	buffer[0] = HIGHER_BYTE;
	buffer[1] = LOWER_BYTE;

	if ((res=write(FILE_DESC,buffer,2)) != 2) {
		fprintf(stderr,"Error: seek_EEPROM()- Failed to write to the i2c bus: %s, res=%d\n",strerror(errno), res);
		return -1;
	}
	printf("seek successful at address=%d\n", address);
	usleep(SLEEP_INTERVAL);
	return 0;
}


/*
 * to write a sequence of count pages to an EEPROM device starting from the current page position of the EEPROM
 * 64*count bytes are stored in a memory buffer pointed by buf
 * Return 0 if succeed, or –1 otherwise
 */
int write_EEPROM(const void *buf, int count){
	printf("write_EEPROM()\n");
	int i=0;
	int res;
	if(!buf || (count<1)){
		printf("buffer is NULL or count<1\n");
		return -1;
	}

	for(i=0; i<count; i++){
		char *buffer = malloc(sizeof(PAGE_SIZE)+2);
		if(!buffer){
			printf("bad malloc\n");
			return -1;
		}
		buffer[0] = HIGHER_BYTE;						// append two byte address while writing
		buffer[1] = LOWER_BYTE;

		memcpy(buffer+2, buf+(i*PAGE_SIZE), PAGE_SIZE);
		//		for(j=0; j<PAGE_SIZE+2; j++)
		//			printf("%c", buffer[j]);
		//		printf("\n");

		if ((res=write(FILE_DESC,buffer,PAGE_SIZE+2)) != PAGE_SIZE+2) {
			fprintf(stderr,"Error: Failed to write to the i2c bus: %s, res=%d\n",strerror(errno), res);
			return -1;
		}
		printf("write successful at address %d\n", LOWER_BYTE + (HIGHER_BYTE<<8));
		usleep(SLEEP_INTERVAL);
		updateAddress();
	}
	/*	after every write, address pointer of the user-app points
		to the next page but the internal address pointer of EEPROM
		is still pointing to the address set by the last write
		eg. Lets say initially user app addr ptr is at 0, then after 2 writes,
		user-app addr ptr is at 2, but EEPROM addr ptr will be at 1 */
	uint address = LOWER_BYTE + (HIGHER_BYTE<<8);
	seek_EEPROM(address/PAGE_SIZE);
	return 0;
}





/*
 * int read_EEPROM(void *buf, int count): to read a sequence of count pages from the EEPROM
device into the user memory pointed by buf. The pages to be read start from the current page position
of the EEPROM. The page position is then advanced by count and, if reaching the end of pages, wrapped
around to the beginning of the EEPROM. Return 0 if succeed, or –1 otherwise.
 */
int read_EEPROM(void *buf, int count){

	printf("read_EEPROM()\n");
	if(!buf || (count<1)){
		printf("buffer is NULL or count<1\n");
		return -1;
	}
	int i, res, k;
	unsigned char *buffer = malloc(count*PAGE_SIZE);

	for(i=0; i<count; i++){									// looping through since there is a limit in i2c-dev read() of 8192
		if ((res=read(FILE_DESC,buffer+i*PAGE_SIZE,PAGE_SIZE)) != PAGE_SIZE) {
			fprintf(stderr,"Error: Failed to read from the i2c bus: %s\n",strerror(errno));
			return -1;
		}
		printf("read successful at address=%d\n", LOWER_BYTE + (HIGHER_BYTE<<8));
		for(k=0; k<PAGE_SIZE; k++){
			printf("%c", buffer[i*PAGE_SIZE+k]);
		}
		printf("\n");
		usleep(SLEEP_INTERVAL);
		updateAddress();
	}

	//	for(i=0; i<PAGE_SIZE*count; i++){
	//		printf("%c", buffer[i]);
	//	}
	printf("\n");
	memcpy(buf, buffer, count*PAGE_SIZE);

	return 0;
}



int main(){

	int i;
	int page_count=20;
	char buf[(page_count*PAGE_SIZE)+1];
	char filename[40];
	snprintf(filename, 19, "/dev/i2c-%d", ADAPTER_NO);	//19 is max no of bytes to be used + 1 for '\0'

	if ((FILE_DESC = open(filename,O_RDWR)) < 0) {		// not fopen so that writes to the bus are not buffered.
		fprintf(stderr,"Error: Failed to open the bus: %s\n",strerror(errno));
		exit(1);
	}
	printf("File opened successfully\n");

	/*	Setup the i2c device address
	 * 	The calls to read and write after the ioctl will automatically set the
	 * 	proper read and write bit when signaling the peripheral.
	 */
	if (ioctl(FILE_DESC,I2C_SLAVE,EEPROM_ADDR) < 0) {
		fprintf(stderr,"Error: Cannot communicate with slave: %s\n",strerror(errno));
		exit(1);

	}
	printf("i2c slave address set\n");


	for(i=0; i<(page_count*PAGE_SIZE); i++){					// Populate the buffer to write
		buf[i]='a';
	}

	seek_EEPROM(500);											// write 15 pages starting from page number = 500
	write_EEPROM(buf, page_count);

	seek_EEPROM(500);											// seek to page number 500

	memset(buf, '0', page_count*PAGE_SIZE);						// memset buffer before reuse
	read_EEPROM(buf, page_count);								// read 15 pages starting from page number = 500

	buf[page_count*PAGE_SIZE] = '\0';
	printf("Read returns successfully with \n %s \n", buf);		// printf buffer

	return 0;
}
