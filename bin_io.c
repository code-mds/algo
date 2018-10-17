#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "constants.h"
#include "bin_io.h"

/*
 * Global variables
 */

FILE* openReadBinary(const char * filename) {
    FILE * filePtr = fopen(filename, "rb");
    if(filePtr == NULL) {
        perror("cannot open file in [rb] mode");
    }
    return filePtr;
}

FILE* openWriteBinary(const char * filename) {
    FILE * filePtr = fopen(filename, "wb");
    if(filePtr == NULL) {
        perror("cannot open file in [wb] mode");
    }
    return filePtr;
}

/*
 *  Read a binary file in chunk
 *  for each char read call a callback function
 */
int readBinaryFile(const char *filename, void (*processChar)(unsigned char)) {
    FILE * filePtr = openReadBinary(filename);

    unsigned char buffer[BLOCK_SIZE];
    size_t bytesRead = 0;
    // read up to sizeof(buffer) bytes
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), filePtr)) > 0)
    {
        for(int i=0;i<bytesRead;i++)
            processChar(buffer[i]);
    }
    fclose(filePtr);
    return 0;
}

/*
 * Diagnostic functions
 */
void traceCharBin(unsigned char ch) {
    for (int bitPos = 7; bitPos >= 0; --bitPos) {
        char val = bit_check(ch, bitPos);
        putchar(val);
    }
    printf("\n");
}

void traceCharBinMsg(const char *msg, unsigned char ch) {
    trace(msg);
    traceCharBin(ch);
}

void trace(const char *msg, ...) {
    va_list args;

    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
}


/*
 * return '1' if the bit at bit_pos is 1, otherwise '0'
 */
char bit_check(unsigned char ch, unsigned int bit_pos) {
    unsigned int val = (ch & (1u << bit_pos));
    return val ? BIT_1 : BIT_0;
}

void bit_set_one(unsigned char * ch, unsigned int bit_pos) {
    *ch |= (1u << bit_pos);
}

void bit_set_zero(unsigned char * ch, unsigned int bit_pos) {
    *ch  &= ~(1u << bit_pos);
}

void bit_copy(unsigned char * byte_to, unsigned char byte_from, unsigned int read_pos, unsigned int write_pos, unsigned int size) {
    for(unsigned int offset=0; offset < size; offset++) {

        unsigned int from = read_pos + offset;
        unsigned int to = write_pos + offset;

        unsigned int bit;
        bit = (byte_from >> from) & 1u;            /* Get the source bit as 0/1 value */
        *byte_to &= ~(1u << to);                  /* clear destination bit */
        *byte_to |= (bit << to);  /* set destination bit */
    }
}
