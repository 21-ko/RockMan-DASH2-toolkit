/*******************************************************************************
 *  
 *  Filename:  MELTTIMTool.c
 *  
 *  Description:  This program compresses and decompresses files using the
 *  DASH2 compression algorithm.
 *  
 *  Author:  happy_land
 *  Date:  2024-06-18
 *  Last update:  2024-09-14
 *  
 *******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>
#include <ctype.h>

#define HEADER_SIZE     0x30
#define WINDOW_SIZE     0x2000
#define WORD_INVALID    0xffff
#define MAX_CODED       ((7 << 1) + 2)
#define MAX_UNCODED     (2 << 1)

#define DEBUG 0

#if DEBUG
#define DEBUG_PRINT(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( 0 )
#else
#define DEBUG_PRINT(...) do{ } while ( 0 )
#endif

/*==============================================================*/
/*	���� ���� ���� �Լ�											*/
/*==============================================================*/
// �������� Ư�� �����¿��� ������ ���̸�ŭ�� ���� �����Ͽ� ������ ��ȯ
unsigned int unpack_data(const char *data, int offset, int length) {
    unsigned int result = 0;
    for (int i = 0; i < length; i++) {
        result |= ((unsigned char)data[offset + i]) << (8 * i);
    }
    return result;
}

// ���ۿ� �ܾ ��ŷ�ϰ� ���� �������� ��ȯ
unsigned short pack_into_buffer(char *buffer, unsigned int destination, unsigned short word) {
    buffer[destination] = word & 0xff;
    buffer[destination + 1] = (word >> 8) & 0xff;
    return destination + 2;
}

// MELT_TIM ����ü
typedef struct {
    uint32_t timEnum;               // offset: 0x00, value: 0x03
    uint32_t decompressedSize;     // offset: 0x04
    uint32_t paddedDataSizeNum;    // offset: 0x08
    uint16_t paletteFramebufferX;  // offset: 0x0c
    uint16_t paletteFramebufferY;  // offset: 0x0e
    uint16_t paletteColors;        // offset: 0x10
    uint16_t numberOfPalettes;     // offset: 0x12
    uint16_t imageFramebufferX;    // offset: 0x14
    uint16_t imageFramebufferY;    // offset: 0x16
    uint16_t imageWidthBytes;      // offset: 0x18
    uint16_t imageHeight;          // offset: 0x1a
    uint16_t dummy[4];
    uint16_t bitfieldSize;         // offset: 0x24
    uint16_t dummy_[5];
} MELT_TIMHeader;

// ������ ���� ���� �Լ�
unsigned int decompress_data(const char *compressed_data, const char *header_data, char **decompressed_data) {
    MELT_TIMHeader header;
    
    // ��� �б�
    header.timEnum = unpack_data(header_data, 0x00, 0x04);
    if (header.timEnum != 0x03) {
        printf("It is not a compressed TIM.\n");
        exit(1);
    }
    
    header.decompressedSize = unpack_data(header_data, 0x04, 0x04);
    header.bitfieldSize = unpack_data(header_data, 0x24, 0x02);

    unsigned int decompress_size = header.decompressedSize;
    unsigned short bitfield_length = header.bitfieldSize;

    if (bitfield_length == 0) {
        fprintf(stderr, "Invalid bitfield length\n");
        return 0;
    }

    char *bitfield = (char *)malloc(bitfield_length * 8);
    if (!bitfield) {
        perror("Failed to allocate memory for bitfield");
        return 0;
    }

    // ��Ʈ�ʵ� �б� (���� ����)
    // 0 = ���ͷ�
    // 1 = ����
    for (int i = 0; i < bitfield_length; i += 4) {
        unsigned int current_bit = unpack_data(compressed_data, 0 + i, 4);
        for (int j = 31; j >= 0; j--) {
            bitfield[i * 8 + (31 - j)] = (current_bit >> j) & 1;
        }
    }

    char *buffer = (char *)malloc(decompress_size);
    if (!buffer) {
        perror("Failed to allocate memory for buffer");
        free(bitfield);
        return 0;
    }

    unsigned int destination = 0, window = 0, payload_offset = bitfield_length;
    for (int i = 0; i < bitfield_length * 8; i++) {
        if (destination >= decompress_size) {
            break;
        }
        unsigned short word = unpack_data(compressed_data, payload_offset, 0x02);
        if (!bitfield[i]) {
            destination = pack_into_buffer(buffer, destination, word);
            DEBUG_PRINT("Literal word: 0x%04x\n", word);
        } else if (word == WORD_INVALID) {
            window += WINDOW_SIZE;
            DEBUG_PRINT("Window incremented: 0x%04x\n", window);
        } else {
            unsigned int source_offset = window + ((word >> 3) & 0x1fff);
            unsigned short length = (word & 0x07) + 2;
            DEBUG_PRINT("Copying from offset: 0x%04x, length: 0x%04x\n", source_offset, length);
            while (length > 0) {
                if (destination >= decompress_size) {
                    break;
                }
                unsigned short packed_word = unpack_data(buffer, source_offset, 0x02);
                destination = pack_into_buffer(buffer, destination, packed_word);
                source_offset += 2;
                length--;
            }
        }
        payload_offset += 2;
    }

    free(bitfield);
    *decompressed_data = buffer;
    return decompress_size;
}

/*==============================================================*/
/*	���� ���� �Լ�												*/
/*==============================================================*/
// ����Ʈ �迭 ����ü
typedef struct {
    uint8_t *data;
    size_t size;
} ByteArray;

// ������ �о� ByteArray ����ü�� ��ȯ�ϴ� �Լ�
ByteArray read_file(const char *filename, size_t offset, size_t limit) {
	FILE *file = NULL;
	errno_t err = fopen_s(&file, filename, "rb");
	
    if (err != 0 || file == NULL) {
        fprintf(stderr, "Failed to open file\n");
        exit(1);
    }

    // ���� ũ�� Ȯ��
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // �������� ���� ũ�⺸�� ū ��� ���� ó��
    if (offset >= size) {
        fprintf(stderr, "Offset is beyond the end of the file\n");
        fclose(file);
        exit(1);
    }

    // ���� ũ�� ���
    size_t read_size = size - offset;
    if (limit && read_size > limit) {
        read_size = limit;
    }

    // �޸� �Ҵ�
    uint8_t *data = (uint8_t *)malloc(read_size);
    if (!data) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        exit(1);
    }

    // ������ ��ġ�� �̵�
    fseek(file, offset, SEEK_SET);

    // ���� �б�
    fread(data, 1, read_size, file);
    fclose(file);

    // ByteArray ����ü ��ȯ
    ByteArray byteArray = {data, read_size};
    return byteArray;
}

// ��Ʈ ��Ʈ���� �����ϴ� ����ü
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint32_t buffer;
    int buffer_count;
} BitStream;

// BitStream �ʱ�ȭ �Լ�
void init_bitstream(BitStream *bs) {
    bs->data = (uint8_t *)malloc(1);
    bs->size = 0;
    bs->capacity = 1;
    bs->buffer = 0;
    bs->buffer_count = 0;
}

// ��Ʈ�� �߰��ϴ� �Լ�
void add_bits(BitStream *bs, uint32_t bits, int count) {
    bs->buffer = (bs->buffer << count) | (bits & ((1 << count) - 1));
    bs->buffer_count += count;

    while (bs->buffer_count >= 32) {
        bs->buffer_count -= 32;
        if (bs->size + 4 > bs->capacity) {
            bs->capacity *= 2;
            bs->data = (uint8_t *)realloc(bs->data, bs->capacity);
        }
        uint32_t out_bits = bs->buffer >> bs->buffer_count;
        bs->data[bs->size++] = (out_bits >> 0) & 0xFF;
        bs->data[bs->size++] = (out_bits >> 8) & 0xFF;
        bs->data[bs->size++] = (out_bits >> 16) & 0xFF;
        bs->data[bs->size++] = (out_bits >> 24) & 0xFF;
        bs->buffer &= (1 << bs->buffer_count) - 1;
    }
}

// ��Ʈ�� ���������� �����ϴ� �Լ�
void finalize_bits(BitStream *bs) {
    if (bs->buffer_count > 0) {
        bs->buffer <<= (32 - bs->buffer_count);
        if (bs->size + 4 > bs->capacity) {
            bs->capacity *= 2;
            bs->data = (uint8_t *)realloc(bs->data, bs->capacity);
        }
        bs->data[bs->size++] = (bs->buffer >> 0) & 0xFF;
        bs->data[bs->size++] = (bs->buffer >> 8) & 0xFF;
        bs->data[bs->size++] = (bs->buffer >> 16) & 0xFF;
        bs->data[bs->size++] = (bs->buffer >> 24) & 0xFF;
        bs->buffer = 0;
        bs->buffer_count = 0;
    }
}

// BitStream�� �����͸� �߰��ϴ� �Լ�
void add_payload(BitStream *bs, const uint8_t *data, size_t size) {
    if (bs->size + size > bs->capacity) {
        while (bs->size + size > bs->capacity) {
            bs->capacity *= 2;
        }
        bs->data = (uint8_t *)realloc(bs->data, bs->capacity);
    }
    memcpy(bs->data + bs->size, data, size);
    bs->size += size;
}

// �����̵� �����츦 ����Ͽ� ���� �� ��ġ�� ã�� �Լ�
void find_match(uint8_t *data, size_t pos, size_t len, size_t *match_pos, size_t *match_len) {
    size_t search_pos = pos / WINDOW_SIZE * WINDOW_SIZE;
    if (pos >= len) {
        *match_pos = 0;
        *match_len = 0;
        return;
    }

    size_t length = len - pos;
    if (length < MAX_UNCODED) {
        *match_pos = 0;
        *match_len = 0;
        return;
    }

    size_t max_match_length = 0;
    size_t max_match_position = 0;

    for (size_t i = search_pos; i < pos; ++i) {
        size_t current_match_length = 0;
        while (current_match_length < length && i + current_match_length < pos &&
               data[i + current_match_length] == data[pos + current_match_length]) {
            current_match_length++;
            if (current_match_length >= MAX_CODED) {
                current_match_length = MAX_CODED;
                break;
            }
        }

        if (current_match_length > max_match_length) {
            max_match_length = current_match_length;
            max_match_position = i;
        }
    }

    if (max_match_length < MAX_UNCODED) {
        *match_pos = 0;
        *match_len = 0;
        return;
    }

    *match_pos = max_match_position;
    *match_len = max_match_length;
}

// �����͸� �����ϴ� �Լ�
uint8_t *compress_data(const char *input_file, const char *header_file, unsigned int header_offset, size_t *final_size) {
    ByteArray src = read_file(input_file, 0, 0);
    ByteArray org_header = read_file(header_file, header_offset, HEADER_SIZE);

    BitStream bits;
    init_bitstream(&bits);

    BitStream payload;
    init_bitstream(&payload);

    size_t pos = 0;
    size_t next_insert_point = WINDOW_SIZE;

    while (pos < src.size) {
        size_t match_pos, match_len;
        find_match(src.data, pos, src.size, &match_pos, &match_len);

        DEBUG_PRINT("Position: 0x%04x, Match offset: 0x%04x, Match length: 0x%04x\n", pos, match_pos, match_len);

        if (match_len >= MAX_UNCODED && match_len % 2 == 0) {
            add_bits(&bits, 1, 1);
            uint16_t offset = match_pos & 0x1FFF;
            uint16_t length = (match_len / 2) - 2;
            uint16_t word = (offset << 3) | (length & 0x07);
            add_payload(&payload, (uint8_t *)&word, 2);
            pos += match_len;
        } else {
            add_bits(&bits, 0, 1);
            if (pos + 1 < src.size) {
                uint16_t word = src.data[pos] | (src.data[pos + 1] << 8);
                add_payload(&payload, (uint8_t *)&word, 2);
                pos += 2;
            } else {
                add_payload(&payload, &src.data[pos], 1);
                pos++;
            }
        }

        if (pos >= next_insert_point) {
            add_bits(&bits, 1, 1);
            uint16_t end_marker = WORD_INVALID;
            add_payload(&payload, (uint8_t *)&end_marker, 2);
            next_insert_point += WINDOW_SIZE;
        }
    }

    finalize_bits(&bits);
    add_payload(&bits, payload.data, payload.size);

    size_t bit_len = bits.size - payload.size;
    *((uint32_t *)(org_header.data + 0x04)) = (uint32_t)src.size;  // org_header�� 0x04�� ���������� ũ�� �ۼ�
    *((uint16_t *)(org_header.data + 0x24)) = (uint16_t)bit_len;  // org_header�� 0x24�� ��Ʈ�ʵ� ���� �ۼ�

    *final_size = org_header.size + bits.size;
    uint8_t *final_data = (uint8_t *)malloc(*final_size);
    memcpy(final_data, org_header.data, org_header.size);
    memcpy(final_data + org_header.size, bits.data, bits.size);

    free(src.data);
    free(org_header.data);
    free(bits.data);
    free(payload.data);

    return final_data;
}

/*==============================================================*/
/*	���� �Լ�													*/
/*==============================================================*/
void to_uppercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper((unsigned char)str[i]);
    }
}

void remove_extension(char *filename) {
    char *dot = strrchr(filename, '.');
    if (dot && dot != filename) {
        *dot = '\0';
    }
}

const char* get_dirname(const char* path) {
    static char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* last_slash = strrchr(dir, '/');
    if (!last_slash) last_slash = strrchr(dir, '\\');
    if (last_slash) *last_slash = '\0';
    else strcpy(dir, ".");
    return dir;
}

// ���� ���� �Լ�
int write_file(const char *filename, uint8_t *data, size_t size) {
    FILE *file = NULL;
    errno_t err = fopen_s(&file, filename, "wb");
    
    if (err != 0 || file == NULL) {
        perror("Unable to open a file");
        return 1;
    }

    fwrite(data, 1, size, file);
    fclose(file);
    return 0;
}

// ���� �κ� ����� �Լ�
int overwrite_file(const char *filename, uint8_t *data, size_t size, unsigned int offset) {
    FILE *file = NULL;
    errno_t err = fopen_s(&file, filename, "r+b");
    
    if (err != 0 || file == NULL) {
        perror("Unable to open the file for overwriting");
        return 1;
    }

    fseek(file, offset, SEEK_SET);
    fwrite(data, 1, size, file);
    fclose(file);
    return 0;
}

int decompress_file(const char *input_file, const char *output_file, const char *header_file, unsigned int header_offset) {
    // ����� ������ �б�
    ByteArray compressed_data = read_file(input_file, 0, 0);
    
    // ��� ������ �б�
    ByteArray header_data = read_file(header_file, header_offset, HEADER_SIZE);

    // ���� ������ ������ ������ ���� �ʱ�ȭ
    char *decompressed_data = NULL;

    // ���� ����
    unsigned int decompress_size = decompress_data((const char *)compressed_data.data, (const char *)header_data.data, &decompressed_data);

    // ���� ������ �����͸� ���Ͽ� ����
    if (decompress_size > 0) {
        if (write_file(output_file, (uint8_t *)decompressed_data, decompress_size)) {
            free(compressed_data.data);
            free(header_data.data);
            free(decompressed_data);
            return 1;
        }
    }

    // �޸� ����
    free(compressed_data.data);
    free(header_data.data);
    free(decompressed_data);

    return 0;
}

int compress_file(const char *input_file, const char *output_file, const char *header_file, unsigned int header_offset) {
    size_t final_size;
    
    uint8_t *compressed_data = compress_data(input_file, header_file, header_offset, &final_size);

    if (compressed_data != NULL) {
        // ��� ����
        uint8_t header_data[HEADER_SIZE];
        memcpy(header_data, compressed_data, HEADER_SIZE);

        // header_file�� header_offset ��ġ�� header_data�� �����
        if (overwrite_file(header_file, header_data, HEADER_SIZE, header_offset) != 0) {
            free(compressed_data);
            return 1;
        }

        // compressed_data�� ������ �����͸� output_file�� ����
        if (write_file(output_file, compressed_data + HEADER_SIZE, final_size - HEADER_SIZE) != 0) {
            free(compressed_data);
            return 1;
        }

        free(compressed_data);
        return 0;
    } else {
        fprintf(stderr, "Failed to compress %s\n", input_file);
        return 1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: %s c|d <input_file> [<original_file>] [<output_folder>]\n", argv[0]);
        return 1;
    }

    clock_t start_time, end_time;
    double time_taken;

    // �Է� ������ �⺻ �̸��� ó�� 4�ڸ� ����
    char input_file_prefix[5] = {0};
    strncpy(input_file_prefix, basename(argv[2]), 4);
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)input_file_prefix[i])) {
            fprintf(stderr, "Error: Input file name prefix is not a decimal number.\n");
            return 1;
        }
    }

    unsigned int prefix_value = atoi(input_file_prefix);
    unsigned int header_offset = prefix_value * HEADER_SIZE;

    if (strcmp(argv[1], "d") == 0) {
        char output_file[512];
        char base[512];
        strncpy(base, basename(argv[2]), sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        remove_extension(base);

        snprintf(output_file, sizeof(output_file), "%s.PIX", base);
        to_uppercase(output_file);

        char output_path[1024];
        if (argc == 5) {
            snprintf(output_path, sizeof(output_path), "%s/%s", argv[4], output_file);
        } else {
            snprintf(output_path, sizeof(output_path), "./%s", output_file);
        }

        // HEADER.BIN ��� ����
        char header_path[1024];
        snprintf(header_path, sizeof(header_path), "%s/HEADER.BIN", get_dirname(argv[2]));

        start_time = clock();
        int result = decompress_file(argv[2], output_path, header_path, header_offset);
        end_time = clock();

        time_taken = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        printf("Decompression took %f seconds\n", time_taken);
        return result;
    } else if (strcmp(argv[1], "c") == 0) {
        if (argc < 4 || argc > 5) {
            fprintf(stderr, "Usage: %s c <input_file> <original_file>\n", argv[0]);
            return 1;
        }

        // original_file ��θ� output_path�� ���
        const char *output_path = argv[3];
        
        // HEADER.BIN ��� ����
        char header_path[1024];
        snprintf(header_path, sizeof(header_path), "%s/HEADER.BIN", get_dirname(argv[3]));

        start_time = clock();
        int result = compress_file(argv[2], output_path, header_path, header_offset);
        end_time = clock();

        time_taken = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        printf("Compression took %f seconds\n", time_taken);
        return result;
    } else {
        fprintf(stderr, "Invalid command. Use 'c' for compression and 'd' for decompression.\n");
        return 1;
    }
}

/*==============================================================*/
/*	"MELTTIMTool.c"	End of File									*/
/*==============================================================*/