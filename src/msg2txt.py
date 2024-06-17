# -*- coding: cp949 -*-
"""
msg2txt.py

Description: Script to convert MSG to TXT.
Date: 24-06-17
Author: happy_land
"""

import os
import sys
import struct
from functools import partial

# 바이트 타입
def read_byte(f):
    return struct.unpack('B', f.read(1))[0]

def read_short(f):
    return struct.unpack('>H', f.read(2))[0]

def read_long(f):
    return struct.unpack('>I', f.read(4))[0]

TBC = '%H'  # 2바이트 기호
FBC = '%I'  # 4바이트 기호

# 제어 코드 함수
def get_pos(f, _):
    b2 = read_short(f)
    b3 = read_short(f)
    b4 = read_byte(f)
    b5 = read_byte(f)
    return f'pos({TBC}{b2},{TBC}{b3},{b4},{b5})'

def _1_52_(f, _):
    b2 = ord(f.read(1))
    codes = ','.join([f'{TBC}{read_short(f)}' for _ in range(5)])
    return f'_fb15({b2},{codes})'

def control_code_legacy(f, control_code_full, a, b, c):
    codes = ''
    if a == 'len':
        length = read_byte(f)
        a = length
        codes = f'{length},'

    for _ in range(a):
        if b == 1:
            code = read_byte(f)
        elif b == 2:
            code = read_short(f)
            code = f'{TBC}{code}'
        elif b == 4:
            code = read_long(f)
            code = f'{FBC}{code}'
        else:
            raise ValueError(f'Invalid value for b: {b}')
        codes += f'{code},'

    result = f'({codes[:-1]})' if a else ''
    return f'_{control_code_full:02x}{result}' if c == 0 else f'{c}{result}'

# 제어 코드 매핑
LEGACY_CODES = {
    0xfb04: (1, 2, 0),
    0xfb05: (2, 1, 0),
    0xfb07: (2, 1, 0),
    0xfb08: (1, 2, 'wait'),
    0xfb09: (1, 1, 'vwait'),
    0xfb0a: (1, 1, 'base_color'),
    0xfb0b: (0, 0, 'init_color'),
    0xfb0c: (0, 0, 0),
    0xfb0d: (4, 1, 0),
    0xfb0e: (1, 1, 0),
    0xfb0f: (1, 1, 'sel'),
    0xfb10: ('len', 2, 0),
    0xfb11: ('len', 1, 0),
    0xfb12: (2, 1, 0),
    0xfb13: (7, 1, 0),
    0xfb16: (4, 1, 0),
    0xfb18: (0, 0, 'pagekey'),
    0xfb19: (1, 2, 0),
    0xfb1a: (1, 4, 'voiceload'),
    0xfb1b: (0, 0, 0),
    0xfb1c: (0, 0, 0),
    0xfb1d: (0, 0, 0),
    0xfb1f: (0, 0, 0),
    0xfb20: (2, 1, 'item_name'),
    0xfb21: (2, 1, 0),
    0xfb22: (1, 1, 'button'),
    0xfb23: (1, 1, 0),
    0xfb24: (0, 0, 'endkey'),
    0xfb25: (1, 1, 0),
    0xfb26: (2, 1, 0),
    0xfb27: (2, 1, 0),
    0xfb28: (4, 1, 0),
    0xfb29: (1, 1, 0),
    0xfb2a: (5, 1, 0),
    0xfb2b: (0, 0, 0),
    0xfb2c: (1, 1, 0),
    0xfb2e: (1, 1, 'space'),
    0xfb30: (1, 1, 0),
    0xfb31: (0, 0, 0),
    0xfb33: (1, 1, 0),
    0xfb34: (2, 1, 0),
    0xfb35: (2, 1, 0),
    0xfb37: (5, 1, 0),
    0xfb39: ('len', 2, 0),
    0xfb3c: (429, 1, 0),
    0xfb3d: (0, 0, 0),
    0xfb3e: (2, 1, 0),
    0xfb3f: (3, 1, 0),
    0xfb40: (2, 1, 0),
    0xfb41: (3, 1, 0),
    0xfb42: (7, 1, 0),
    0xfb44: (1, 1, 0),
    0xfb45: (1, 1, 0),
    0xfb47: (4, 1, 0),
    0xfb48: (1, 1, 0),
    0xfb49: (1, 1, 0),
    0xfb4a: (1, 1, 0),
    0xfb4b: (1, 1, 0),
    0xfb4c: ('len', 2, 0),
    0xfd00: (3, 1, 'nextpage')
}

CONTROL_CODE_LIST = {code: partial(control_code_legacy, a=a, b=b, c=c) for code, (a, b, c) in LEGACY_CODES.items()}
CONTROL_CODE_LIST[0xfb06] = get_pos
CONTROL_CODE_LIST[0xfb15] = _1_52_

# 문자 로드 함수
def load_moji_table(file_path):
    moji_dict = {}
    with open(file_path, 'r', encoding='utf-8') as file:
        for line in file:
            if '=' in line:
                key, value = line.strip().split('=')
                key_bytes = bytes.fromhex(key)
                moji_dict[key_bytes] = value
    return moji_dict

NEW_LINE = '\n'
TAB = '\t'

def get_pointers(filename, offset):
    with open(filename, 'rb') as f:
        f.seek(offset)
        pointer_count = struct.unpack('H', f.read(2))[0] // 2
        f.seek(offset)
        return [struct.unpack('H', f.read(2))[0] for _ in range(pointer_count)]

def calculate_difference(pointers):
    return [pointers[i + 1] - pointers[i] for i in range(len(pointers) - 1)]

def decode_with_moji_and_controlcode(filename, offset, moji_dict, is_func, pointers, pointer_diff):
    with open(filename, 'rb') as f:
        decoded_texts = []

        for i, diff in enumerate(pointer_diff):
            f.seek(offset + pointers[i])
            text_parts = []

            if is_func == 1:
                start_code = read_short(f)
                text_parts.append(f'func({TBC}{start_code})')

            last_was_control_code = False

            for _ in range(diff):
                byte = f.read(1)
                byte_val = ord(byte)

                if 0x00 <= byte_val <= 0xE9:
                    text_parts.append(byte)
                    last_was_control_code = False
                elif 0xF8 <= byte_val <= 0xFA:
                    text_parts.append(byte + f.read(1))
                    last_was_control_code = False
                elif byte_val in [0xFB, 0xFD]:
                    b1 = f.read(1)
                    control_code = (byte_val << 8) + ord(b1)
                    control_code_func = CONTROL_CODE_LIST.get(control_code)

                    if not last_was_control_code:
                        text_parts.append(NEW_LINE + TAB)

                    if control_code_func:
                        text_parts.append(control_code_func(f, control_code))
                        text_parts.append(NEW_LINE + TAB)
                    else:
                        text_parts.append(f'Unrecognized control code: 0x{control_code:04x}')
                    last_was_control_code = True
                elif byte_val == 0xFC:
                    text_parts.append('\tline' + NEW_LINE + TAB)
                elif byte_val == 0xFE:
                    byte = f.read(1)
                    byte_val = ord(byte)

                    if 0x00 <= byte_val <= 0xE9:
                        text_parts.append(byte)
                        last_was_control_code = False
                    elif 0xF8 <= byte_val <= 0xFA:
                        byte += f.read(1)
                        text_parts.append(byte)
                        last_was_control_code = False

                    x = read_byte(f)
                    y = read_byte(f)
                    for _ in range(x):
                        rubi_char = f.read(1)
                elif byte_val == 0xFF:
                    if text_parts and text_parts[-1] == (NEW_LINE + TAB):
                        text_parts.pop()
                    text_parts.append(NEW_LINE + TAB + 'end')
                    break

            decoded_text = ''.join([moji_dict.get(byte, '?') if isinstance(byte, bytes) else byte for byte in text_parts])
            decoded_texts.append((i, decoded_text))

    return decoded_texts

def write_to_file(file_name, texts):
    try:
        with open(file_name, 'w', encoding='utf-8') as file:
            for i, text in texts:
                file.write(f'//{i:04d}:\n')
                file.write(f'{TAB}{text}')
                file.write(f'{NEW_LINE}{TAB}--\n\n')
    except FileNotFoundError:
        print(f"{file_name} not found.")

def parse_off_param(offset_param):
    if '+' in offset_param:
        return sum(int(value, 16) if '0x' in value else int(value) for value in offset_param.split('+'))
    return int(offset_param, 16) if offset_param.startswith('0x') else int(offset_param)

def main():
    if len(sys.argv) < 4:
        print("Usage: python msg2txt.py <input_file> <output_file> <offset_param> [<is_func>]")
        sys.exit(1)

    filename, output_file, offset_param = sys.argv[1:4]
    is_func = sys.argv[4] if len(sys.argv) > 4 else False
    is_func = int(is_func)
    if is_func < 0 or is_func >= 2:
        print("Invalid option")
        sys.exit(1)

    offset = parse_off_param(offset_param)

    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # 포인터 계산
    pointers = get_pointers(filename, offset)
    pointer_diff = calculate_difference(pointers)
    
    # 문자 리스트 읽기
    if not os.path.exists('Moji.tbl'):
        print("Character list file is missing")
        sys.exit(1)
    moji_dict = load_moji_table('Moji.tbl')
    
    # 디코딩 후 저장
    decoded_texts = decode_with_moji_and_controlcode(filename, offset, moji_dict, is_func, pointers, pointer_diff)
    write_to_file(output_file, decoded_texts)

if __name__ == '__main__':
    main()