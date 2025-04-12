#include <cmath>
#include <vector>
#include <string>
#include <format>
#include <stdio.h>
#include <errno.h>
#include <windows.h>

#include "PureDOOM.h"

doom_key_t win32_keycode_to_doom_key(int win32_keycode);
bool key_status[256];

#pragma region sixel
#define SIXEL_START "\x1bPq"
#define SIXEL_END   "\x1b\\"
#define MAX_COLORS  256
//#define MAX_COLORS  16 // a playable fps

typedef struct {
    uint8_t r, g, b;
    int used;
} ColorPalette;

ColorPalette palette[MAX_COLORS];
int palette_size = 0;
int palette_inited = 0;

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

void generate_palette(const unsigned char *data, int width, int height, int channels) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int idx = (i * width + j) * channels;
            uint8_t r = data[idx];
            uint8_t g = data[idx + 1];
            uint8_t b = data[idx + 2];

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

std::string encode_sixel(const unsigned char *img, int width, int height, int channels) {
    std::string result;
    result += SIXEL_START;
    result += std::format("\"1;1;{:d};{:d}", width, height);

    for (int i = 0; i < palette_size; i++) {
        result += std::format("#{:d};2;{:d};{:d};{:d}", i,
                              palette[i].r * 100 / 255,
                              palette[i].g * 100 / 255,
                              palette[i].b * 100 / 255);
    }

    for (int y = 0; y < height; y += 6) {
        int band_height = (height - y) < 6 ? (height - y) : 6;

        for (int c = 0; c < palette_size; c++) {
            result += std::format("#{:d}", c);

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

                result += char(sixel_byte + 0x3F);
            }
            result += "$";
        }
        result += "-";
    }

    result += SIXEL_END;
    return result;
}
#pragma endregion sixel

int main(int argc, char **args)
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bufferInfo;
    GetConsoleScreenBufferInfo(output, &bufferInfo);

    //-----------------------------------------------------------------------
    // Setup DOOM
    //-----------------------------------------------------------------------

    // Change default bindings to modern
//    doom_set_default_int("key_up", DOOM_KEY_W);
//    doom_set_default_int("key_down", DOOM_KEY_S);
//    doom_set_default_int("key_strafeleft", DOOM_KEY_A);
//    doom_set_default_int("key_straferight", DOOM_KEY_D);
    doom_set_default_int("key_use", DOOM_KEY_E);
    doom_set_default_int("key_fire", DOOM_KEY_SPACE);
    doom_set_default_int("mouse_move", 0); // Mouse will not move forward

    // Setup resolution
    doom_set_resolution(SCREENWIDTH, SCREENHEIGHT);

    // Initialize doom
    doom_init(argc, args, DOOM_FLAG_MENU_DARKEN_BG);

    std::string result;

    while(true) {

        for (int i = 0; i < 256; i++) {
            if (GetAsyncKeyState(i) & 0x8000) {
                // GetAsyncKeyState & 0x8000 keydown
                doom_key_down(win32_keycode_to_doom_key(i));
                key_status[i] = true;
            }
            else if (GetAsyncKeyState(i) & 0b1) {
                // GetAsyncKeyState & 0b1    keypress
            } else {
                if (key_status[i]) {
                    doom_key_up(win32_keycode_to_doom_key(i));
                    key_status[i] = false;
                }
            }
        }

        doom_update();

        const unsigned char* image = doom_get_framebuffer(3);
        generate_palette(image, SCREENWIDTH, SCREENHEIGHT, 3);
        result = encode_sixel(image, SCREENWIDTH, SCREENHEIGHT, 3);

        SetConsoleCursorPosition(output, bufferInfo.dwCursorPosition);
        printf(result.c_str());
    }

    return EXIT_SUCCESS;
}

doom_key_t win32_keycode_to_doom_key(int win32_keycode) {
    switch (win32_keycode) {
        case VK_TAB: return DOOM_KEY_TAB;
        case VK_RETURN: return DOOM_KEY_ENTER;
        case VK_ESCAPE: return DOOM_KEY_ESCAPE;
        case VK_SPACE: return DOOM_KEY_SPACE;
        case VK_LEFT: return DOOM_KEY_LEFT_ARROW;
        case VK_UP: return DOOM_KEY_UP_ARROW;
        case VK_RIGHT: return DOOM_KEY_RIGHT_ARROW;
        case VK_DOWN: return DOOM_KEY_DOWN_ARROW;
        default: return (doom_key_t)win32_keycode;
    }

    return DOOM_KEY_UNKNOWN;
}
