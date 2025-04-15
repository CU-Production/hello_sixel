#include <cmath>
#include <vector>
#include <string>
#include <format>
#include <stdio.h>
#include <errno.h>
#include <windows.h>

#define ENABLE_SOUND 1
#define ENABLE_LCD   1
uint8_t audio_read(uint16_t addr);
void audio_write(uint16_t addr, uint8_t val);
#include "peanut_gb.h"
extern "C" {
#include "minigb_apu/minigb_apu.h"
}

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

struct priv_t
{
	uint8_t *rom;
	uint8_t *cart_ram;

    /* Frame buffer */
    uint32_t fb[LCD_HEIGHT][LCD_WIDTH];
};

uint8_t gb_rom_read(gb_s *gb, const uint_fast32_t addr) {
	const priv_t* const p = (const priv_t* const)gb->direct.priv;
	return p->rom[addr];
}

uint8_t gb_cart_ram_read(gb_s *gb, const uint_fast32_t addr) {
	const priv_t* const p = (const priv_t* const)gb->direct.priv;
	return p->cart_ram[addr];
}

void gb_cart_ram_write(gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
	const priv_t* const p = (const priv_t* const)gb->direct.priv;
	p->cart_ram[addr] = val;
}

uint8_t* read_rom_to_ram(const char *file_name) {
	FILE *rom_file = fopen(file_name, "rb");
	size_t rom_size;
	uint8_t *rom = NULL;

	if(rom_file == NULL)
		return NULL;

	fseek(rom_file, 0, SEEK_END);
	rom_size = ftell(rom_file);
	rewind(rom_file);
	rom = (uint8_t*)malloc(rom_size);

	if(fread(rom, sizeof(uint8_t), rom_size, rom_file) != rom_size) {
		free(rom);
		fclose(rom_file);
		return NULL;
	}

	fclose(rom_file);
	return rom;
}

void gb_error(gb_s *gb, const enum gb_error_e gb_err, const uint16_t val) {
	const char* gb_err_str[GB_INVALID_MAX] = {
		"UNKNOWN",
		"INVALID OPCODE",
		"INVALID READ",
		"INVALID WRITE",
		"HALT FOREVER"
	};
	priv_t* priv = (priv_t*)gb->direct.priv;

	fprintf(stderr, "Error %d occurred: %s at %04X\n. Exiting.\n",
			gb_err, gb_err_str[gb_err], val);

	/* Free memory and then exit. */
	free(priv->cart_ram);
	free(priv->rom);
	exit(EXIT_FAILURE);
}

#if ENABLE_LCD
void lcd_draw_line(gb_s *gb, const uint8_t pixels[160], const uint_fast8_t line) {
	priv_t* priv = (priv_t*)gb->direct.priv;
	const uint32_t palette[] = { 0xFFFFFF, 0xA5A5A5, 0x525252, 0x000000 };

    for(unsigned int x = 0; x < LCD_WIDTH; x++)
        priv->fb[line][x] = palette[pixels[x] & 3];
}
#endif

static struct minigb_apu_ctx apu;

uint8_t audio_read(uint16_t addr) {
    return minigb_apu_audio_read(&apu, addr);
}

void audio_write(uint16_t addr, uint8_t val) {
    minigb_apu_audio_write(&apu, addr, val);
}

void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    minigb_apu_audio_callback(&apu, (audio_sample_t*)pOutput);
}

#pragma region sixel
#define SIXEL_START "\x1bPq"
#define SIXEL_END   "\x1b\\"
#define MAX_COLORS  256

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

std::string encode_sixel(unsigned char *img, int width, int height, int channels) {
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "%s ROM\n", argv[0]);
        exit(EXIT_FAILURE);
    }

	/* Must be freed */
	char *rom_file_name = argv[1];
	static gb_s gb;
	static priv_t priv;
	enum gb_init_error_e ret;

	if((priv.rom = read_rom_to_ram(rom_file_name)) == NULL) {
		printf("%d: %s\n", __LINE__, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read, &gb_cart_ram_write, &gb_error, &priv);

	if(ret != GB_INIT_NO_ERROR) {
		printf("Error: %d\n", ret);
		exit(EXIT_FAILURE);
	}

	priv.cart_ram = (uint8_t*)malloc(gb_get_save_size(&gb));

#if ENABLE_LCD
	gb_init_lcd(&gb, &lcd_draw_line);
//    gb.direct.interlace = 1;
#endif

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bufferInfo;
    GetConsoleScreenBufferInfo(output, &bufferInfo);

    // init miniaudio
    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_s16;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = AUDIO_SAMPLE_RATE;
    deviceConfig.periodSizeInFrames = AUDIO_SAMPLES;
//    deviceConfig.noFixedSizedCallback = false;
//    deviceConfig.periodSizeInFrames = 64;
    deviceConfig.dataCallback = audio_callback;
    deviceConfig.pUserData = (void*)&apu;

    if (ma_device_init(nullptr, &deviceConfig, &device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to open playback device.");
        return EXIT_FAILURE;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to start playback device.");
        ma_device_uninit(&device);
        return EXIT_FAILURE;
    }

    minigb_apu_audio_init(&apu);

    double dt = 0;
    bool running = true;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> prev_time{std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())};
    while(running) {
        std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> time{std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())};
        dt = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(time - prev_time).count()) / 1000.0f;
        prev_time = time;

        gb.direct.joypad = 0xff;
        for (int i = 0; i < 256; i++) {
            if ((GetAsyncKeyState(i) & 0x8000) || (GetAsyncKeyState(i) & 0b1)) {
                // GetAsyncKeyState & 0x8000 keydown
                // GetAsyncKeyState & 0b1    keypress
                if (i == VK_ESCAPE)
                    running = false;
                else {
                    gb.direct.joypad_bits.a	     = !(i == 'z' || i == 'Z');
                    gb.direct.joypad_bits.b	     = !(i == 'x' || i == 'X');
                    gb.direct.joypad_bits.select = !(i == VK_BACK)        ;
                    gb.direct.joypad_bits.start	 = !(i == VK_RETURN)      ;
                    gb.direct.joypad_bits.up	 = !(i == VK_UP)          ;
                    gb.direct.joypad_bits.down	 = !(i == VK_DOWN)        ;
                    gb.direct.joypad_bits.left	 = !(i == VK_LEFT)        ;
                    gb.direct.joypad_bits.right	 = !(i == VK_RIGHT)       ;

#if ENABLE_LCD
                    gb.direct.frame_skip = (i == VK_SPACE);
//                    gb.direct.interlace = (i == 'i' || i == 'I');
#endif
                }
            }
        }

        gb_run_frame(&gb);

        unsigned char* image = (unsigned char*)priv.fb;
        generate_palette(image, LCD_WIDTH, LCD_HEIGHT, 4);
        std::string result = encode_sixel(image, LCD_WIDTH, LCD_HEIGHT, 4);

        SetConsoleCursorPosition(output, bufferInfo.dwCursorPosition);
        printf(result.c_str());

        using namespace std::chrono_literals;
        double time_to_16ms = (1.0 / 60) - dt;
        if (time_to_16ms > 0)
            std::this_thread::sleep_for(1.0s * time_to_16ms);// NOLINT magic numbers
    }

    ma_device_uninit(&device);
	free(priv.cart_ram);
	free(priv.rom);

	return EXIT_SUCCESS;
}
