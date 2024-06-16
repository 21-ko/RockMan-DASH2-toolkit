/*******************************************************************************
 *  
 *  Filename:  MELTTIMTool.c
 *  
 *  Description:  This program compresses and decompresses files using the
 *  DASH2 compression algorithm.
 *  
 *  Author:  happy_land
 *  Date:  2024-06-17
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
/*	압축 해제 관련 함수											*/
/*==============================================================*/
// 데이터의 특정 오프셋에서 지정된 길이만큼의 값을 언팩하여 정수로 반환
unsigned int unpack_data(const char *data, int offset, int length) {
    unsigned int result = 0;
    for (int i = 0; i < length; i++) {
        result |= ((unsigned char)data[offset + i]) << (8 * i);
    }
    return result;
}

// 버퍼에 단어를 패킹하고 다음 목적지를 반환
unsigned short pack_into_buffer(char *buffer, unsigned int destination, unsigned short word) {
    buffer[destination] = word & 0xff;
    buffer[destination + 1] = (word >> 8) & 0xff;
    return destination + 2;
}

// MELT_TIM 구조체
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

// 데이터 압축 해제 함수
unsigned int decompress_data(const char *compressed_data, const char *header_data, char **decompressed_data) {
    MELT_TIMHeader header;
    
    // 헤더 읽기
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

    // 비트필드 읽기 (압축 여부)
    // 0 = 리터럴
    // 1 = 참조
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
/*	압축 관련 함수												*/
/*==============================================================*/
// 바이트 배열 구조체
typedef struct {
    uint8_t *data;
    size_t size;
} ByteArray;

// 파일을 읽어 ByteArray 구조체로 반환하는 함수
ByteArray read_file(const char *filename, size_t offset, size_t limit) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file\n");
        exit(1);
    }

    // 파일 크기 확인
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 오프셋이 파일 크기보다 큰 경우 에러 처리
    if (offset >= size) {
        fprintf(stderr, "Offset is beyond the end of the file\n");
        fclose(file);
        exit(1);
    }

    // 읽을 크기 계산
    size_t read_size = size - offset;
    if (limit && read_size > limit) {
        read_size = limit;
    }

    // 메모리 할당
    uint8_t *data = (uint8_t *)malloc(read_size);
    if (!data) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        exit(1);
    }

    // 오프셋 위치로 이동
    fseek(file, offset, SEEK_SET);

    // 파일 읽기
    fread(data, 1, read_size, file);
    fclose(file);

    // ByteArray 구조체 반환
    ByteArray byteArray = {data, read_size};
    return byteArray;
}

// 비트 스트림을 저장하는 구조체
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint32_t buffer;
    int buffer_count;
} BitStream;

// BitStream 초기화 함수
void init_bitstream(BitStream *bs) {
    bs->data = (uint8_t *)malloc(1);
    bs->size = 0;
    bs->capacity = 1;
    bs->buffer = 0;
    bs->buffer_count = 0;
}

// 비트를 추가하는 함수
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

// 비트를 최종적으로 정리하는 함수
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

// BitStream에 데이터를 추가하는 함수
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

// 슬라이딩 윈도우를 사용하여 가장 긴 매치를 찾는 함수
void find_match(uint8_t *data, size_t pos, size_t len, size_t *match_pos, size_t *match_len) {
    size_t search_start = pos / WINDOW_SIZE * WINDOW_SIZE;
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

    for (size_t i = search_start; i < pos; i++) {
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

// 데이터를 압축하는 함수
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
    *((uint32_t *)(org_header.data + 0x04)) = (uint32_t)src.size;  // org_header의 0x04에 압축해제된 크기 작성
    *((uint16_t *)(org_header.data + 0x24)) = (uint16_t)bit_len;  // org_header의 0x24에 비트필드 길이 작성

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
/*	전역 함수													*/
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

// 파일 쓰기 함수
int write_file(const char *filename, uint8_t *data, size_t size) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Unable to open a file");
        return 1;
    }

    fwrite(data, 1, size, file);
    fclose(file);
    return 0;
}

// 파일 부분 덮어쓰기 함수
int overwrite_file(const char *filename, uint8_t *data, size_t size, unsigned int offset) {
    FILE *file = fopen(filename, "r+b");
    if (!file) {
        perror("Unable to open the file for overwriting");
        return 1;
    }

    fseek(file, offset, SEEK_SET);
    fwrite(data, 1, size, file);
    fclose(file);
    return 0;
}

int decompress_file(const char *input_file, const char *output_file, const char *header_file, unsigned int header_offset) {
    // 압축된 데이터 읽기
    ByteArray compressed_data = read_file(input_file, 0, 0);
    
    // 헤더 데이터 읽기
    ByteArray header_data = read_file(header_file, header_offset, HEADER_SIZE);

    // 압축 해제된 데이터 저장할 변수 초기화
    char *decompressed_data = NULL;

    // 압축 해제
    unsigned int decompress_size = decompress_data((const char *)compressed_data.data, (const char *)header_data.data, &decompressed_data);

    // 압축 해제된 데이터를 파일에 쓰기
    if (decompress_size > 0) {
        if (write_file(output_file, (uint8_t *)decompressed_data, decompress_size)) {
            free(compressed_data.data);
            free(header_data.data);
            free(decompressed_data);
            return 1;
        }
    }

    // 메모리 해제
    free(compressed_data.data);
    free(header_data.data);
    free(decompressed_data);

    return 0;
}

int compress_file(const char *input_file, const char *output_file, const char *header_file, unsigned int header_offset) {
    size_t final_size;
    
    uint8_t *compressed_data = compress_data(input_file, header_file, header_offset, &final_size);

    if (compressed_data != NULL) {
        // 헤더 추출
        uint8_t header_data[HEADER_SIZE];
        memcpy(header_data, compressed_data, HEADER_SIZE);

        // header_file의 header_offset 위치에 header_data를 덮어쓰기
        if (overwrite_file(header_file, header_data, HEADER_SIZE, header_offset) != 0) {
            free(compressed_data);
            return 1;
        }

        // compressed_data의 나머지 데이터를 output_file에 쓰기
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

    // 입력 파일의 기본 이름의 처음 4자를 추출
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

        // HEADER.BIN 경로 설정
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

        // original_file 경로를 output_path로 사용
        const char *output_path = argv[3];
        
        // HEADER.BIN 경로 설정
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