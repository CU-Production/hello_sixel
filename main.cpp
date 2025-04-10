#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <format>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define SIXEL_START "\x1bP0;0;8q"
//#define SIXEL_START "\x1bPq"
#define SIXEL_END "\x1b\\"
#define MAX_COLORS 256

typedef struct {
    uint8_t r, g, b;
    int used;
} ColorPalette;

ColorPalette palette[MAX_COLORS];
int palette_size = 0;

int find_closest_color(uint8_t r, uint8_t g, uint8_t b) {
    int min_dist = 255*3;
    int index = 0;

    for (int i = 0; i < palette_size; i++) {
        int dr = abs(r - palette[i].r);
        int dg = abs(g - palette[i].g);
        int db = abs(b - palette[i].b);
        int dist = dr + dg + db;

        if (dist < min_dist) {
            min_dist = dist;
            index = i;
        }
    }
    return index;
}

void generate_palette(unsigned char *data, int width, int height, int channels) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int idx = (i * width + j) * channels;
            uint8_t r = data[idx];
            uint8_t g = data[idx + 1];
            uint8_t b = data[idx + 2];

//            r = (r >> 5) * 36;
//            g = (g >> 5) * 36;
//            b = (b >> 6) * 85;

            int found = 0;
            for (int k = 0; k < palette_size; k++) {
                if (palette[k].r == r && palette[k].g == g && palette[k].b == b) {
                    found = 1;
                    break;
                }
            }

            if (!found && palette_size < MAX_COLORS) {
                palette[palette_size].r = r;
                palette[palette_size].g = g;
                palette[palette_size].b = b;
                palette_size++;
            }
        }
    }
}

void encode_sixel(unsigned char *img, int width, int height, int channels) {
    printf(SIXEL_START);
    printf("\"1;1;%d;%d", width, height);

    for (int i = 0; i < palette_size; i++) {
        printf("#%d;2;%d;%d;%d", i,
               palette[i].r * 100 / 255,
               palette[i].g * 100 / 255,
               palette[i].b * 100 / 255);
    }

    for (int y = 0; y < height; y += 6) {
        std::string band_height_str;

        int band_height = (height - y) < 6 ? (height - y) : 6;

        for (int c = 0; c < palette_size; c++) {
            band_height_str += std::format("#{:d}", c);

            for (int x = 0; x < width; x++) {
                uint8_t sixel_byte = 0;

                for (int dy = 0; dy < band_height; dy++) {
                    int current_y = y + dy;
                    if (current_y >= height) break;

                    int idx = (current_y * width + x) * channels;
                    uint8_t r = img[idx];
                    uint8_t g = img[idx + 1];
                    uint8_t b = img[idx + 2];

                    int color_idx = find_closest_color(r, g, b);
                    if (color_idx == c) {
                        sixel_byte |= (1 << dy);
                    }
                }

                band_height_str += char(sixel_byte + 0x3F);
            }
            band_height_str += "$";
        }
        band_height_str += "-";
        printf(band_height_str.c_str());
    }

    printf(SIXEL_END);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image-file>\n", argv[0]);
        return 1;
    }

    int width, height, channels;
    unsigned char *img = stbi_load(argv[1], &width, &height, &channels, 0);
    if (!img) {
        fprintf(stderr, "Error loading image: %s\n", stbi_failure_reason());
        return 1;
    }

    // 初始化调色板
    generate_palette(img, width, height, channels);

    // 编码输出Sixel
    encode_sixel(img, width, height, channels);

    stbi_image_free(img);
    return 0;
}