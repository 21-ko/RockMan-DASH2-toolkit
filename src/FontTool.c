/*******************************************************************************
 *  
 *  Filename:  FontTool.c
 *  
 *  Description:  This is a program to separate/combine fonts.
 *  
 *  Author:  happy_land
 *  Date:  2024-06-17
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

size_t read_file(const char* filename, uint32_t** buffer) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    size_t filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    *buffer = (uint32_t*)malloc(filesize);
    if (!*buffer) {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    size_t read_size = fread(*buffer, 1, filesize, file);
    if (read_size != filesize) {
        perror("Error reading file");
        exit(EXIT_FAILURE);
    }

    fclose(file);
    return filesize / sizeof(uint32_t);
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

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <combine|split> <input file 1> [<input file 2> <output file>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint32_t *tp1, *tp2, *out;
    size_t size1, size2;

    if (strcmp(argv[1], "combine") == 0) {
        if (argc != 5) {
            fprintf(stderr, "Usage for combine: %s combine <input file 1> <input file 2> <output file>\n", argv[0]);
            return EXIT_FAILURE;
        }
        size1 = read_file(argv[2], &tp1);
        size2 = read_file(argv[3], &tp2);

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
            fprintf(stderr, "Usage for split: %s split <input file>\n", argv[0]);
            return EXIT_FAILURE;
        }
        size1 = read_file(argv[2], &out);
        size2 = size1;

        tp1 = (uint32_t*)malloc(size1 * sizeof(uint32_t));
        tp2 = (uint32_t*)malloc(size2 * sizeof(uint32_t));

        if (!tp1 || !tp2) {
            perror("Memory allocation error");
            return EXIT_FAILURE;
        }

        bit_split(tp1, tp2, out, size1);
        write_file("FONT1.PIX", tp1, size1);
        write_file("FONT2.PIX", tp2, size2);

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