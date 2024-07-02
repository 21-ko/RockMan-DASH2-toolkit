/*******************************************************************************
 *  
 *  Filename:  FontTool.c
 *  
 *  Description:  This is a program to separate/combine fonts.
 *  
 *  Author:  happy_land
 *  Date:  2024-06-18
 *  
 *******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void bit_combine(uint32_t* out, uint32_t* tp1, uint32_t* tp2, size_t size) {
    for (size_t cnt = 0; cnt < size; cnt++) {
        out[cnt] = (tp1[cnt] & 0x33333333) | ((tp2[cnt] & 0x33333333) << 2);
    }
}

void bit_split(uint32_t* tp1, uint32_t* tp2, uint32_t* combined, size_t size) {
    for (size_t cnt = 0; cnt < size; cnt++) {
        tp1[cnt] = combined[cnt] & 0x33333333;
        tp2[cnt] = (combined[cnt] >> 2) & 0x33333333;
    }
}

size_t read_file(const char* filename, size_t offset, uint32_t** buffer) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    size_t filesize = ftell(file);
    if (offset >= filesize) {
        fprintf(stderr, "Offset is beyond the end of the file\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fseek(file, offset, SEEK_SET);

    size_t read_size = filesize - offset;
    *buffer = (uint32_t*)malloc(read_size);
    if (!*buffer) {
        perror("Memory allocation error");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    size_t actual_read_size = fread(*buffer, 1, read_size, file);
    if (actual_read_size != read_size) {
        perror("Error reading file");
        fclose(file);
        free(*buffer);
        exit(EXIT_FAILURE);
    }

    fclose(file);
    return actual_read_size / sizeof(uint32_t);
}

void write_file(const char* filename, uint32_t* buffer, size_t size) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening file for writing");
        exit(EXIT_FAILURE);
    }

    size_t write_size = fwrite(buffer, sizeof(uint32_t), size, file);
    if (write_size != size) {
        perror("Error writing file");
        exit(EXIT_FAILURE);
    }

    fclose(file);
}

void create_tim_header(uint8_t* header, uint32_t* palette, size_t palette_size) {
    uint32_t tim_magic = 0x00000010;
    uint32_t color_depth = 8;
    uint32_t clut_len = 0x10C;
    uint16_t palette_framebuffer_x = 0;
    uint16_t palette_framebuffer_y = 0;
    uint16_t colors = 0x10;
    uint16_t clut_num = 0x08;
    
    uint32_t img_len = 0x0000800C;
    uint16_t image_framebuffer_x = 0;
    uint16_t image_framebuffer_y = 0;
    uint16_t image_width = 0x40;
    uint16_t image_height = 256;


    memcpy(header, &tim_magic, 4);
    memcpy(header + 4, &color_depth, 4);
    memcpy(header + 8, &clut_len, 4);
    memcpy(header + 12, &palette_framebuffer_x, 2);
    memcpy(header + 14, &palette_framebuffer_y, 2);
    memcpy(header + 16, &colors, 2);
    memcpy(header + 18, &clut_num, 2);
    memcpy(header + 20, palette, palette_size);
    memcpy(header + 276, &img_len, 4);
    memcpy(header + 280, &image_framebuffer_x, 2);
    memcpy(header + 282, &image_framebuffer_y, 2);
    memcpy(header + 284, &image_width, 2);
    memcpy(header + 286, &image_height, 2);
}

void append_palette(const char* clt_file, const char* tim_file1, const char* tim_file2) {
    FILE* clt = fopen(clt_file, "rb");
    if (!clt) {
        perror("Error opening CLT file");
        exit(EXIT_FAILURE);
    }

    uint32_t palette[256]; // 0x100 bytes

    fread(palette, 1, 0x100, clt);
    fseek(clt, 0x100, SEEK_SET);
    fclose(clt);

    uint8_t header1[288];
    uint8_t header2[288];

    create_tim_header(header1, palette, 0x100);
    create_tim_header(header2, palette, 0x100);

    FILE* tim1 = fopen(tim_file1, "rb+");
    FILE* tim2 = fopen(tim_file2, "rb+");
    if (!tim1 || !tim2) {
        perror("Error opening TIM files");
        exit(EXIT_FAILURE);
    }

    fseek(tim1, 0, SEEK_END);
    size_t size1 = ftell(tim1);
    fseek(tim1, 0, SEEK_SET);

    fseek(tim2, 0, SEEK_END);
    size_t size2 = ftell(tim2);
    fseek(tim2, 0, SEEK_SET);

    uint32_t* buffer1 = (uint32_t*)malloc(size1);
    uint32_t* buffer2 = (uint32_t*)malloc(size2);

    fread(buffer1, 1, size1, tim1);
    fread(buffer2, 1, size2, tim2);

    fclose(tim1);
    fclose(tim2);

    tim1 = fopen(tim_file1, "wb");
    tim2 = fopen(tim_file2, "wb");

    fwrite(header1, sizeof(uint8_t), 288, tim1);
    fwrite(buffer1, 1, size1, tim1);

    fwrite(header2, sizeof(uint8_t), 288, tim2);
    fwrite(buffer2, 1, size2, tim2);

    fclose(tim1);
    fclose(tim2);

    free(buffer1);
    free(buffer2);
}

uint32_t read_offset_value(const char *filename, size_t offset) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("File open error");
        return 0;
    }

    fseek(file, offset, SEEK_SET);

    uint32_t value;
    fread(&value, sizeof(uint32_t), 1, file);

    fclose(file);
    
    return value;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <combine|split> <input folder> [<input file 2> <output file>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint32_t *tp1, *tp2, *out;
    size_t size1, size2;

    if (strcmp(argv[1], "combine") == 0) {
        if (argc != 5) {
            fprintf(stderr, "Usage for combine: %s combine <input file 1> <input file 2> <output file>\n", argv[0]);
            return EXIT_FAILURE;
        }
        
        uint32_t value1 = read_offset_value(argv[2], 0x08);
        uint32_t value2 = read_offset_value(argv[3], 0x08);
        
        size_t read_size1 = value1 + 0x14;
        size_t read_size2 = value2 + 0x14;
        
        size1 = read_file(argv[2], read_size1, &tp1);
        size2 = read_file(argv[3], read_size2, &tp2);

        if (size1 != size2) {
            fprintf(stderr, "Error: Input files must be of the same size\n");
            return EXIT_FAILURE;
        }

        out = (uint32_t*)malloc(size1 * sizeof(uint32_t));
        if (!out) {
            perror("Memory allocation error");
            return EXIT_FAILURE;
        }

        bit_combine(out, tp1, tp2, size1);
        write_file(argv[4], out, size1);

        free(tp1);
        free(tp2);
        free(out);

    } else if (strcmp(argv[1], "split") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage for split: %s split <input folder>\n", argv[0]);
            return EXIT_FAILURE;
        }

        char input_file[256];
        snprintf(input_file, sizeof(input_file), "%s/0000_INIT.PIX", argv[2]);
        size1 = read_file(input_file, 0, &out);
        size2 = size1;

        tp1 = (uint32_t*)malloc(size1 * sizeof(uint32_t));
        tp2 = (uint32_t*)malloc(size2 * sizeof(uint32_t));

        if (!tp1 || !tp2) {
            perror("Memory allocation error");
            return EXIT_FAILURE;
        }

        bit_split(tp1, tp2, out, size1);

        char output_file1[256];
        char output_file2[256];

        snprintf(output_file1, sizeof(output_file1), "FONT1.TIM");
        snprintf(output_file2, sizeof(output_file2), "FONT2.TIM");

        write_file(output_file1, tp1, size1);
        write_file(output_file2, tp2, size2);

        char clt_file[256];
        snprintf(clt_file, sizeof(clt_file), "%s/0001_INIT.CLT", argv[2]);
        append_palette(clt_file, output_file1, output_file2);

        free(tp1);
        free(tp2);
        free(out);

    } else {
        fprintf(stderr, "Invalid operation. Use 'combine' or 'split'.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*==============================================================*/
/*	"FontTool.c"	End of File									*/
/*==============================================================*/