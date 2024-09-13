# -*- coding: cp949 -*-
"""
combbin.py

Description: Script to merge or split BIN archives.
Author: happy_land
Date: 24-06-16
Last update: 24-09-14

Functionality:
- Merge multiple BIN files into a single archive file.
- Split a single BIN archive file into multiple files.
"""

import os
import glob
import struct
import sys

HEADER_SIZE = 0x30
CHUNK_SIZE = 0x800
PADDED_CLUT_SIZE = CHUNK_SIZE - HEADER_SIZE
MAX_CLUT_SIZE = 0x100
exception_list = ["MTIM", "VAB", "VAB2"]

BITS4 = 8  # 4bpp
BITS8 = 9  # 8bpp

def pad_to_multiple_of(value, multiple):
    """�־��� ���� multiple�� ����� �ǵ��� �е�"""
    return (value + multiple - 1) // multiple * multiple

def read_tim_headers(file_path):
    """TIM ������ ������� ����"""
    with open(file_path, 'rb') as f:
        # TIM ��� �б�
        tim_magic, color_type, clut_len, palette_framebuffer_x, palette_framebuffer_y, colors, clut_num = struct.unpack('<IIIHHHH', f.read(0x14))

        # CLUT ������ �б�
        if color_type == BITS8:
            clut_data = f.read(MAX_CLUT_SIZE << 1)
        else:
            clut_data = f.read(MAX_CLUT_SIZE)

        # �̹��� ��� �б�
        img_len, image_framebuffer_x, image_framebuffer_y, image_width, image_height = struct.unpack('<IHHHH', f.read(12))
        
    return (palette_framebuffer_x, palette_framebuffer_y), clut_data, (image_framebuffer_x, image_framebuffer_y, image_width, image_height), color_type

def combine_files(input_folder, output_folder):
    header_filename = os.path.join(input_folder, 'HEADER.BIN')
    output_filename = os.path.join(output_folder, os.path.basename(input_folder) + '.BIN')

    with open(header_filename, 'rb') as header_file:
        header_data = header_file.read()

    # ���� ������ ���
    num_files = len(header_data) // HEADER_SIZE

    # ���ǿ� �´� ��� ���� ã��
    folder_name = os.path.basename(input_folder)
    file_patterns = os.path.join(input_folder, '[0-9][0-9][0-9][0-9]_' + folder_name + '.*')
    all_files = glob.glob(file_patterns)

    # ���� �׷�ȭ
    file_groups = {}
    for file in all_files:
        base_name = os.path.splitext(os.path.basename(file))[0]
        if base_name in file_groups:
            raise ValueError(f"Error: Duplicate numeric file name found: {base_name}")
        file_groups[base_name] = file

    if len(file_groups) != num_files:
        raise ValueError(f"Error: Number of files ({len(file_groups)}) does not match number of headers ({num_files})")

    os.makedirs(output_folder, exist_ok=True)
    with open(output_filename, 'wb') as output_file:
        for index in range(num_files):
            # �ش� �ε����� �ش��ϴ� ��� ����
            file_header = bytearray(header_data[index * HEADER_SIZE:(index + 1) * HEADER_SIZE])
            file_name = f'{index:04d}_{folder_name}'
            if file_name not in file_groups:
                raise ValueError(f"Error: File {file_name} not found.")
                
            file_path = file_groups[file_name]
            file_extension = os.path.splitext(file_path)[1][1:].upper()

            with open(file_path, 'rb') as input_file:
                file_data = input_file.read()

            # Ȯ���ڰ� TIM�� ���
            if file_extension == 'TIM':
                tim_header, clut_data, image_header, color_type = read_tim_headers(file_path)

                # TIM ����� ���ο� ����� �����
                struct.pack_into('<HH', file_header, 0x0c, *tim_header)
                struct.pack_into('<HHHH', file_header, 0x14, *image_header)

                # CLUT �����͸� �е��Ͽ� ��� ������ ����
                clut_padded_size = pad_to_multiple_of(len(clut_data), CHUNK_SIZE)
                clut_padding_needed = clut_padded_size - len(clut_data) - HEADER_SIZE
                clut_data += (b'\x00' * clut_padding_needed)
                # ���� ��� ���� (�ȼ� �����͸�)
                if color_type == BITS8:
                    file_data = file_data[0x220:]
                else:
                    file_data = file_data[0x120:]  # 4��Ʈ
                file_data = clut_data + file_data
            elif file_extension == 'PIX':
                file_data = (b'\x00' * PADDED_CLUT_SIZE) + file_data

            # 0x800�� ����� ���߱� ���� �е� ���
            padded_size = pad_to_multiple_of(HEADER_SIZE + len(file_data), CHUNK_SIZE)
            padding_needed = padded_size - (HEADER_SIZE + len(file_data))

            # ���� ũ�⸦ ������Ʈ
            if not file_extension in exception_list:
                struct.pack_into('<I', file_header, 0x04, len(file_data))
            # �е��� ũ�⸦ ������Ʈ
            padded_size_num = padded_size // CHUNK_SIZE
            struct.pack_into('<I', file_header, 0x08, padded_size_num)

            # ����� ���� �����͸� ��� ���Ͽ� ��
            output_file.write(file_header)
            output_file.write(file_data)

            # �е� �߰�
            output_file.write(b'\x00' * padding_needed)

        # ���յ� ������ �������� 0x800�� ���� ������ �߰�
        output_file.write(b'\x00' * CHUNK_SIZE)

    print(f"Combined file created: {output_filename}")

def unpack_u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]
    
"""
������ ����
    N_DAT:      �Ϲ� ���� ������
    TIM:        TIM ����
    VB:         VAB�� ���� ������
    PIX:        VRAM �̹���
    CLT:        �÷� ������
    VAB:        VAB ����
    SEQ:        ���� SEQ ����
    SEP:        ���� SEP ����
    TIM_C:      TIM�� CLUT ��ǥ�� TIM ���� ���� ��ϵǾ� �ִ� ��
    TIM_C2:     TIM�� CLUT ��ǥ�� TIM ���� ���� ��ϵǾ� �ִ� ��(64dot�� ����?)
    TIM_P:      TIM�� �ȼ� ��ǥ�� TIM ���� ���� ��ϵǾ� �ִ� ��
    MELT:       ����� ������
    MELT_TIM:   ����� TIM ������
"""

# EBD = Enemy Binary Data

# 0x05, 0x0E ���� ����� ���� (���� ����)
# 0x08, 0x0F ���� ����� ����

def get_extension(file_kind, header_data):
    if file_kind == 0x00000000:
        return "DAT"  # �Ϲ� ���� ������
    elif file_kind == 0x00000001:
        return "BIN"  # progbin
    elif file_kind == 0x00000002:  # �÷� ������(CLT) �Ǵ� PIX �Ǵ� CLUT + PIX
        number_of_palettes = unpack_u16(header_data, 0x12)  # CLUT ����
        image_width_bytes = unpack_u16(header_data, 0x18)  # �ȼ� ����

        if number_of_palettes >= 1:
            if image_width_bytes >= 1:
                return "TIM"  # �� �� ������
            else:
                return "CLT"  # �ȷ�Ʈ�� �ִٸ�
        else:
            if image_width_bytes >= 1:
                return "PIX"  # �ȼ��� �ִٸ�
        return "TEX"
    elif file_kind == 0x00000003:
        return "MTIM"  # ����� TIM ������
    elif file_kind == 0x00000005:
        return "VAB"
    elif file_kind == 0x00000008:
        return "SEQ"
    elif file_kind == 0x0000000A:
        return "MAM"  # �𵨰� �ִϸ��̼�
    elif file_kind == 0x0000000C:
        return "STG"  # Ÿ�� ���ϱ���
    elif file_kind == 0x0000000D:
        return "IDX"  # STG�� ������ (MDT�� IDX�� �ε����� ���)
    elif file_kind == 0x0000000E:
        return "VAB2"
    elif file_kind == 0x0000000F:
        return "SEQ2"
    elif file_kind == 0x00000010:
        return "MDT"  # �� ������
    elif file_kind == 0x00000012:
        return "MSG"  # �޼��� ������
    elif file_kind == 0x00000015:
        return "021"  # Unknown
    else:
        return f"{file_kind:03d}"

def create_tim_header(header_data, data, file_size):
    # ���� ���� �� ����
    tim_magic = 0x10
    color_depth = BITS4
    clut_len = MAX_CLUT_SIZE + 0x0c  # + 0x0c

    palette_colors = unpack_u16(header_data, 0x10)
    number_of_palettes = unpack_u16(header_data, 0x12)  # �׻� 1?

    palette_framebuffer_x = unpack_u16(header_data, 0x0c)
    palette_framebuffer_y = unpack_u16(header_data, 0x0e)
    clut_data = data[:MAX_CLUT_SIZE]               # ���� ���� 0x100����
    img_len = file_size - PADDED_CLUT_SIZE + 0x0c  # + 0x0c
    image_framebuffer_x = unpack_u16(header_data, 0x14)
    image_framebuffer_y = unpack_u16(header_data, 0x16)
    image_width = unpack_u16(header_data, 0x18)
    image_height = unpack_u16(header_data, 0x1a)
    
    # ������
    colors = 0x10
    clut_num = 0x08
    
    # �� ���� ���� (8��Ʈ)
    if palette_colors >= 0x100:
        color_depth = BITS8
        clut_len = (MAX_CLUT_SIZE << 1) + 0x0c
        clut_data = data[:MAX_CLUT_SIZE << 1]
        colors = 0x100
        clut_num = 0x01

    # ����
    tim_header = struct.pack('<IIIHHHH', tim_magic, color_depth, clut_len, palette_framebuffer_x, palette_framebuffer_y, colors, clut_num)
    Image_header = struct.pack('<IHHHH', img_len, image_framebuffer_x, image_framebuffer_y, image_width, image_height)
    tim_header += clut_data + Image_header
    return tim_header

def extract_files(input_file, output_folder):
    basename = os.path.splitext(os.path.basename(input_file))[0]
    os.makedirs(f"{output_folder}/{basename}", exist_ok=True)
    
    with open(input_file, 'rb') as f:
        file_count = 0
        headers = []

        while True:
            header_data = f.read(HEADER_SIZE)
            if len(header_data) < HEADER_SIZE:
                break
            
            file_kind, file_size, padded_size = struct.unpack('III', header_data[:12])
            padded_size *= CHUNK_SIZE
            if padded_size == 0:  # ���� �� (����)
                break
            
            headers.append(header_data)
            
            file_content_size = padded_size - HEADER_SIZE
            file_content = f.read(file_content_size)  # 0x30 ��� ���� ������
            if len(file_content) < file_content_size:
                print("Error: Could not read the expected padded size")
                break
            
            file_extension = get_extension(file_kind, header_data)
            output_filename = f"{output_folder}/{basename}/{file_count:04d}_{basename}.{file_extension.upper()}"
            
            with open(output_filename, 'wb') as output_file:
                if file_extension == "TIM":
                    tim_header = create_tim_header(header_data, file_content, file_size)
                    file_content = file_content[PADDED_CLUT_SIZE:]  # �ȼ� ������
                    output_file.write(tim_header)
                elif file_extension == "PIX":
                    file_content = file_content[PADDED_CLUT_SIZE:]
                    file_size = len(file_content)
                
                # file_size�� ���� ũ�Ⱑ �ƴ� ���
                if file_extension in exception_list:  # ���� �����
                    output_file.write(file_content)  # �״�� ����
                else:
                    # �е� ����
                    output_file.write(file_content[:file_size])
            
            file_count += 1

    header_file_path = f"{output_folder}/{basename}/HEADER.BIN"
    with open(header_file_path, 'wb') as header_file:
        for header in headers:
            header_file.write(header)
            
    print(f"Extracted files to: {output_folder}/{basename}")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: python combbin.py -c <input_folder> <output_folder>  # combine mode")
        print("       python combbin.py -x <input_file> <output_folder>     # extract mode")
        sys.exit(1)

    mode = sys.argv[1]
    input_path = sys.argv[2]
    output_path = sys.argv[3]

    if mode == '-c':
        combine_files(input_path, output_path)
    elif mode == '-x':
        extract_files(input_path, output_path)
    else:
        print("Invalid mode. Use -c to combine or -x to extract.")
        sys.exit(1)
