import os
from PIL import Image


def convert_image_to_h(input_path):
    img = Image.open(input_path)

    colors = img.getcolors(maxcolors=100000)
    is_gray = 0
    try:
        for count, (r, g, b, _) in colors:
            if (5 < r < 250) or (5 < g < 250) or (5 < b < 250):
                is_gray += 1
    except:
        pass
    print(input_path, len(colors), is_gray)

    if is_gray:
        img_bw = img.convert('1', dither=Image.Dither.FLOYDSTEINBERG)
    else:
        img_bw = img.convert('L').point(lambda p: 255 if p > 128 else 0, mode='1')

    width, height = img_bw.size
    pixels = img_bw.load()
    bytes_list = []

    for y in range(height):
        for x in range(0, width, 8):
            byte_val = 0
            for bit in range(8):
                if x + bit < width:
                    # 0 = white, 1 = black
                    if not pixels[x + bit, y]:
                        byte_val |= (1 << (7 - bit))
            bytes_list.append(f'0x{byte_val:02X}')

    return bytes_list, img_bw



with open('./inkplate6/image.h', 'w', encoding='utf-8') as f:
    f.write('#ifndef _IMAGES_H_\n#define _IMAGES_H_\n\n')
    f.write('#include <Arduino.h>\n\n')

    image_list = []

    for root, _, files in os.walk('./img/'):
        for file in files:
            array_name = '_' + file.split('.')[0].replace('-', '_')
            img_bytes, img_converted = convert_image_to_h(root + file)
            image_list.append((array_name, img_converted))
            img_converted.save('./img1/' + file)
            f.write(f'const unsigned char {array_name}[{len(img_bytes)}] PROGMEM = {{\n    ')
            f.write(', '.join(img_bytes))
            f.write('\n};\n')

    f.write(f'\nconst BackgroundInt backgroundImages[{len(image_list)}] PROGMEM = {{\n')

    image_param_list = []
    for array_name, img_converted in image_list:
        img_params = array_name.split('_')
        month_start = int(img_params[1])
        month_end = int(img_params[2])
        day_start = int(img_params[3])
        day_end = int(img_params[4])
        width, height = img_converted.size
        image_param_list.append(f'    {{.data={array_name}, .width={width}, .height={height}, .month_start={month_start}, .month_end={month_end}, .day_start={day_start}, .day_end={day_end}}}')
    f.write(',\n'.join(image_param_list))

    f.write('\n};\n')

    f.write('\n#endif\n')

