#include <stdio.h>
#include <string.h>

#include "adhuff_decompress.h"
#include "adhuff_common.h"
#include "constants.h"
#include "bin_io.h"
#include "log.h"

/*
 * modules variables
 */
static byte_t           output_buffer[BUFFER_SIZE] = {0};
static unsigned int     output_byte_idx;
static unsigned int     in_bit_idx;
static unsigned int     bits_to_ignore;
static unsigned int     last_bit;

/*
 * Private methods
 */
void    read_header(FILE *inputFilePtr);
long    get_filesize(FILE *input_file_ptr);
int     read_data_cross_bytes(const byte_t input_buffer[], int max_bits_to_read, byte_t sub_buffer[]);
int     decode_new_symbol(const byte_t input_buffer[]);
int     decode_existing_symbol(const byte_t input_buffer[]);
int     flush_uncompressed(FILE *output_file_ptr);
int     skip_nyt_bits(int nyt_size);
void    output_symbol(byte_t symbol);

/*
 * decompress file
 */
int adh_decompress_file(const char input_file_name[], const char output_file_name[]) {
    log_info("adh_decompress_file", "%-40s %s\n", input_file_name, output_file_name);

    int rc = RC_OK;
    FILE * output_file_ptr = NULL;
    FILE * input_file_ptr = bin_open_read(input_file_name);
    if (input_file_ptr == NULL) {
        rc = RC_FAIL;
    }

    if(rc == RC_OK) {
        output_file_ptr = bin_open_create(output_file_name);
        if (output_file_ptr == NULL) {
            rc = RC_FAIL;
        }
    }

    if (rc == RC_OK)
        rc = adh_init_tree();

    if (rc == RC_OK) {
        read_header(input_file_ptr);

        // TODO: handle big files, don't read entire file in memory
        int input_size = get_filesize(input_file_ptr);
        int bytes_to_read = input_size;

        memset(output_buffer, 0, sizeof(output_buffer));
        byte_t input_buffer[input_size];

        // read up to sizeof(buffer) bytes
        size_t bytes_read = fread(input_buffer, sizeof(byte_t), bytes_to_read, input_file_ptr);
        //while ((bytes_read = fread(input_buffer, sizeof(byte_t), bytes_to_read, input_file_ptr)) > 0)
        {
            if(bytes_read != bytes_to_read) {
                log_error("adh_decompress_file", "bytes_read (%zu) != bytes_to_read (%d)\n", bytes_read, bytes_to_read);
                release_resources(output_file_ptr, input_file_ptr);
                return RC_FAIL;
            }

            last_bit = (input_size * SYMBOL_BITS) - bits_to_ignore -1;

            while(last_bit > in_bit_idx) {

                byte_t nyt_encoding[MAX_CODE_BITS] = { 0 };
                int nyt_size = adh_get_NYT_encoding(nyt_encoding);

                bool is_nyt_code = compare_input_and_nyt(input_buffer, in_bit_idx, nyt_encoding, nyt_size);
                if(is_nyt_code) {
                    rc = skip_nyt_bits(nyt_size);
                    if(rc == RC_FAIL) {
                        release_resources(output_file_ptr, input_file_ptr);
                        return rc;
                    }

                    // not coded byte
                    rc = decode_new_symbol(input_buffer);
                    if(rc == RC_FAIL) {
                        release_resources(output_file_ptr, input_file_ptr);
                        return rc;
                    }
                } else {
                    rc = decode_existing_symbol(input_buffer);
                    if(rc == RC_FAIL) {
                        release_resources(output_file_ptr, input_file_ptr);
                        return rc;
                    }
                }

                if(output_byte_idx == BUFFER_SIZE -1) {
                    rc = flush_uncompressed(output_file_ptr);
                    if(rc == RC_FAIL) {
                        release_resources(output_file_ptr, input_file_ptr);
                        return rc;
                    }
                }
            }
        }

        flush_uncompressed(output_file_ptr);

        adh_destroy_tree();
    }

    release_resources(output_file_ptr, input_file_ptr);

    return rc;
}

int skip_nyt_bits(int nyt_size) {
    log_debug("skip_nyt_bits", "in_bit_idx=%-8d nyt_size=%d\n", in_bit_idx, nyt_size);
    in_bit_idx += nyt_size;

    if(in_bit_idx > last_bit) {
        log_error("adh_decompress_file", "too many bits read: in_bit_idx (%d) > last_bit (%d)", in_bit_idx, last_bit);
        return RC_FAIL;
    }
    return RC_OK;
}

int flush_uncompressed(FILE *output_file_ptr) {
    log_debug("flush_uncompressed", "in_bit_idx=%-8d output_byte_idx=%d\n", in_bit_idx, output_byte_idx);

    size_t bytes_written = fwrite(output_buffer, sizeof(byte_t), output_byte_idx, output_file_ptr);
    if(bytes_written != output_byte_idx) {
        log_error("flush_uncompressed", "bytes_written (%zu) != out_byte_idx (%d)\n", bytes_written, output_byte_idx);
        return RC_FAIL;
    }

    output_byte_idx = 0;
    return RC_OK;
}

int decode_existing_symbol(const byte_t input_buffer[]) {
    unsigned short original_input_buffer_bit_idx = in_bit_idx;

    log_debug("decode_existing_symbol", "\n");

    adh_node_t* node = NULL;
    int     bit_array_size = 0;
    byte_t  bit_array[MAX_CODE_BITS] = {0};
    byte_t  sub_buffer[MAX_CODE_BYTES] = {0};
    int     num_bytes = read_data_cross_bytes(input_buffer, MAX_CODE_BITS, sub_buffer);

    for (int byte_idx = 0; byte_idx < num_bytes && node == NULL; ++byte_idx) {
        for (int bit_idx = 0; bit_idx < SYMBOL_BITS && node == NULL; ++bit_idx) {
            if(bit_array_size > MAX_CODE_BITS) {
                log_error("decode_existing_symbol", "bit_array_size (%d) >= MAX_CODE_BITS (%d)", bit_array_size, MAX_CODE_BITS);
                return RC_FAIL;
            }

            // shift left previous bits
            for (int i = bit_array_size; i > 0; --i) {
                bit_array[i] = bit_array[i-1];
            }

            bit_array[0] = bit_check(sub_buffer[byte_idx], SYMBOL_BITS - bit_idx -1);
            bit_array_size++;
            node = adh_search_encoding_in_tree(bit_array, bit_array_size);
        }
    }

    if(node == NULL) {
        log_error("decode_existing_symbol", "cannot find node bit_array_size=%d", bit_array_size);
        return RC_FAIL;
    }

    in_bit_idx = original_input_buffer_bit_idx + bit_array_size;
    output_symbol(node->symbol);
    adh_update_tree(node, false);
    return RC_OK;
}

void output_symbol(byte_t symbol) {
    char symbol_str[50] = {0};
    log_info("  output_symbol", "%s in_bit_idx=%-8d\n",
            fmt_symbol(symbol, symbol_str, sizeof(symbol_str)),
              in_bit_idx);

    output_buffer[output_byte_idx] = symbol;
    output_byte_idx++;
}

int decode_new_symbol(const byte_t input_buffer[]) {
    log_debug("decode_new_symbol", "in_bit_idx=%-8d\n", in_bit_idx);

    byte_t  new_symbol[1] = {0};
    int     num_bytes = read_data_cross_bytes(input_buffer, SYMBOL_BITS, new_symbol);
    if(num_bytes > 1) {
        log_error("decode_new_symbol", "expected 1 byte received %d bytes", num_bytes);
        return RC_FAIL;
    }

    output_symbol(new_symbol[0]);
    adh_node_t * node = adh_create_node_and_append(new_symbol[0]);
    adh_update_tree(node, true);
    return RC_OK;
}

int read_data_cross_bytes(const byte_t input_buffer[], int max_bits_to_read, byte_t sub_buffer[]) {
    log_debug("  read_data_cross_bytes", "in_bit_idx=%-8d max_bits_to_read=%-8d\n", in_bit_idx, max_bits_to_read);

    int temp_buffer_bit_idx = 0;
    int temp_byte_idx = 0;
    while(max_bits_to_read > 0) {
        if(in_bit_idx > last_bit) {
            //TODO ... rewind in_bit_idx ?
            break;
        }

        int available_bits = get_available_bits(in_bit_idx);
        int bits_to_copy = available_bits > max_bits_to_read ? max_bits_to_read : available_bits;

        int read_bit_idx = bit_to_change(in_bit_idx);
        int write_bit_idx = bit_to_change(temp_buffer_bit_idx);

        temp_byte_idx = bit_idx_to_byte_idx(temp_buffer_bit_idx);

        // copy bits from most significant bit to least significant
        // e.g. from 5 and size 4 -> 5,4,3,2
        int input_byte_idx = bit_idx_to_byte_idx(in_bit_idx);
        bit_copy(input_buffer[input_byte_idx], &sub_buffer[temp_byte_idx], read_bit_idx, write_bit_idx, bits_to_copy);

        in_bit_idx += bits_to_copy;
        temp_buffer_bit_idx += bits_to_copy;
        max_bits_to_read -= bits_to_copy;
    }
    return temp_byte_idx + 1;
}

long get_filesize(FILE *input_file_ptr) {
    fseek(input_file_ptr, 0 , SEEK_END);
    long file_size = ftell(input_file_ptr);
    fseek(input_file_ptr, 0 , SEEK_SET);
    return file_size;
}

/*
 * read header
 */
void read_header(FILE *inputFilePtr) {
    log_debug("read_header", "\n");

    byte_t header;
    fread(&header, sizeof(byte_t), 1, inputFilePtr);

    first_byte_union first_byte;
    first_byte.raw = header;

    bits_to_ignore = first_byte.split.header;
    in_bit_idx = HEADER_BITS;
    output_byte_idx = 0;
 }