# -*- coding: cp949 -*-
"""
txt2msg.py

Description: Script to convert TXT to MSG.
Author: happy_land
Date: 24-06-17
Last update: 24-09-14
"""

import re
import os
import sys

def load_moji_list(file_path):
    moji_list = {}
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
        for line in lines:
            value, key = line.strip('\n').split('=')
            value = value.strip()
            try:
                moji_list[key] = bytes.fromhex(value)
            except ValueError:
                print(f"Invalid value: '{value}' in line: '{line.strip()}'")
    return moji_list
    
Moji_list = load_moji_list('Moji.tbl')

def txt_to_bin(input_file):
    global Moji_list
    # 매핑 테이블 정의
    command_table = {
        'func': b'',
        'pos': b'\xfb\x06',
        'wait': b'\xfb\x08',
        'vwait': b'\xfb\x09',
        'base_color': b'\xfb\x0a',
        'init_color': b'\xfb\x0b',
        'sel': b'\xfb\x0f',
        'pagekey': b'\xfb\x18',
        'voiceload': b'\xfb\x1a',
        'item_name': b'\xfb\x20',
        'button': b'\xfb\x22',
        'endkey': b'\xfb\x24',
        'space': b'\xfb\x2e',
        'line': b'\xfc',
        'nextpage': b'\xfd\x00',
        'rubi': b'\xfe',
        'endrubi': b'',
        'end': b'\xff'
    }

    with open(input_file, 'r', encoding='utf-8') as f:
        script = f.read()

    # 블록별로 분할
    blocks = script.split('--')
    # 변환 데이터
    binary_data = b''
    # 포즈필드 (오프셋)
    posfiled = b''
    posfiled_length = len(blocks) << 1

    # 각 블록에 대해 처리
    for block in blocks:
        lines = block.split('\n')

        # 포즈필드에 블록의 시작 오프셋 추가 (포즈필드 길이를 고려)
        posfiled += (len(binary_data) + posfiled_length).to_bytes(2, 'little')

        for line in lines:
            if '//' in line:
                line = line.split('//')[0]

            tokens = re.split(r'(' + '|'.join(command_table.keys()) + r'|_([0-9a-fA-F]{4})(\((.*?)\))?)', line)

            for token in tokens:
                if token is None:
                    continue

                # 명령어 처리
                if token in command_table:
                    binary_data += command_table[token]
                    match = re.search(rf'{token}\((.*?)\)', line)
                    if match:
                        values = match.group(1).split(',')
                        for value in values:
                            if value.startswith('%H'):
                                binary_data += int(value[2:]).to_bytes(2, 'big')
                            elif value.startswith('%I'):
                                binary_data += int(value[2:]).to_bytes(4, 'big')
                            else:
                                binary_data += int(value).to_bytes(1, 'big')
                    line = re.sub(rf'{token}\(.*?\)', '', line)
                    line = line.replace(token, '')

                # 16진수 명령어 처리
                elif token.startswith('_'):
                    match = re.search(r'_([0-9a-fA-F]{4})(\((.*?)\))?', token)
                    if match:
                        binary = bytes([int(match.group(1)[0:2], 16)]) + bytes([int(match.group(1)[2:4], 16)])
                        binary_data += binary
                        if match.group(3):
                            values = match.group(3).split(',')
                            for value in values:
                                if value.startswith('%H'):
                                    binary_data += int(value[2:]).to_bytes(2, 'big')
                                elif value.startswith('%I'):
                                    binary_data += int(value[2:]).to_bytes(4, 'big')
                                else:
                                    binary_data += int(value).to_bytes(1, 'big')

                # 문자 처리
                else:
                    for char in token:
                        if char in Moji_list:
                            binary_data += Moji_list[char]

    binary_data = posfiled + binary_data
    
    return binary_data

def calculate_padded_size(size, block_size):
    return (size + block_size - 1) & ~(block_size - 1)
    
def write_header_to_header_bin(header, directory, offset):
    header_bin_path = os.path.join(directory, "HEADER.BIN")
    with open(header_bin_path, 'r+b') as f:
        f.seek(offset)
        f.write(header)

def main():
    if len(sys.argv) < 2:
        print("Usage: python txt2msg.py <input_file> [<is_header>]")
        sys.exit(1)
    
    input_file = sys.argv[1]
    is_header = sys.argv[2] if len(sys.argv) > 2 else None

    file_name_prefix = os.path.basename(input_file)[:4]

    if not file_name_prefix.isdigit():
        print("Error: The first 4 characters of the input file name must be digits.")
        sys.exit(1)

    base_name = os.path.splitext(input_file)[0]
    output_file = base_name + ".MSG"

    binary_data = txt_to_bin(input_file)

    if is_header and int(is_header) == 1:
        sign_bytes = (0x12).to_bytes(4, 'little')

        size = len(binary_data)
        size_bytes = size.to_bytes(4, 'little')
        size = size + 0x30

        padded_size = calculate_padded_size(size, 0x800)
        padded_size_bytes = (padded_size // 0x800).to_bytes(4, 'little')
        remaining_size = padded_size - size

        bin_dummy_bytes = b'\x00' * 0x24
        padd_bytes = b'\x00' * remaining_size
        header = sign_bytes + size_bytes + padded_size_bytes + bin_dummy_bytes
        binary_data = binary_data + padd_bytes

        # 헤더를 HEADER.BIN에 쓰기
        offset = int(file_name_prefix) * 0x30
        input_file_directory = os.path.dirname(input_file)
        write_header_to_header_bin(header, input_file_directory, offset)

    with open(output_file, 'wb') as f:
        f.write(binary_data)

if __name__ == "__main__":
    main()