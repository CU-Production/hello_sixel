#include <cmath>
#include <vector>
#include <chrono>
#include <string>
#include <format>
#include <windows.h>

#include "NES.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

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

void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    auto apu = (APU*)pDevice->pUserData;
    if (apu == nullptr) {
        return;
    }

    auto* outStream = (float*) pOutput;

    apu->streamMutex.lock();
    for (int i = 0; i < frameCount; i++) {
        if (i < apu->stream.size()) {
            float tmp = apu->stream.front();
            outStream[i * 2 + 0] = tmp;
            outStream[i * 2 + 1] = tmp;
            apu->stream.erase(apu->stream.begin());
        }
    }
    apu->streamMutex.unlock();
}

constexpr auto nes_width  = 256;
constexpr auto nes_height = 240;

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        std::cerr << "Please pass ROM path as first parameter.\n";
        return EXIT_FAILURE;
    }

    char* SRAM_path = new char[strlen(argv[1]) + 1];
    strcpy(SRAM_path, argv[1]);
    strcat(SRAM_path, ".srm");

    std::cout << "Initializing NES..." << std::endl;
    NES* nes = new NES(argv[1], SRAM_path);
    if (!nes->initialized) return EXIT_FAILURE;

    // init miniaudio
    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = 44100;
//    deviceConfig.noFixedSizedCallback = false;
//    deviceConfig.periodSizeInFrames = 64;
    deviceConfig.dataCallback = audio_callback;
    deviceConfig.pUserData = nes->apu;

    if (ma_device_init(nullptr, &deviceConfig, &device) != MA_SUCCESS) {
        std::cout << "Failed to open playback device." << std::endl;
        return EXIT_FAILURE;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cout << "Failed to start playback device." << std::endl;
        ma_device_uninit(&device);
        return EXIT_FAILURE;
    }

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO bufferInfo;
    GetConsoleScreenBufferInfo(output, &bufferInfo);

    // input
    uint8_t controller1 = 0;

    double dt = 0;
    bool running = true;
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> prev_time{std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())};
    while(running) {
        std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> time{std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())};
        dt = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(time - prev_time).count()) / 1000.0f;
        prev_time = time;

        uint8_t ret = 0;
        for (int i = 0; i < 256; i++) {
            if ((GetAsyncKeyState(i) & 0x8000) || (GetAsyncKeyState(i) & 0b1)) {
                // GetAsyncKeyState & 0x8000 keydown
                // GetAsyncKeyState & 0b1    keypress
                if (i == VK_ESCAPE)
                    running = false;
                else {
                    ret |= (i == 'z' || i == 'Z');       // A
                    ret |= (i == 'x' || i == 'X') << 1;  // B
                    ret |= (i == VK_BACK)         << 2;  // Select
                    ret |= (i == VK_RETURN)       << 3;  // Start
                    ret |= (i == VK_UP)           << 4;
                    ret |= (i == VK_DOWN)         << 5;
                    ret |= (i == VK_LEFT)         << 6;
                    ret |= (i == VK_RIGHT)        << 7;
                }
            }
        }
        controller1 = ret;

        // processe input
        nes->controller1->buttons = controller1;
        nes->controller2->buttons = 0;

        // step the NES state forward by 'dt' seconds, or more if in fast-forward
        emulate(nes, dt);

        unsigned char* image = (unsigned char*)nes->ppu->front;
        generate_palette(image, nes_width, nes_height, 4);
        std::string result = encode_sixel(image, nes_width, nes_height, 4);

        SetConsoleCursorPosition(output, bufferInfo.dwCursorPosition);
        printf(result.c_str());

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1.0s / 120.0);// NOLINT magic numbers
    }

    // save SRAM back to file
    if (nes->cartridge->battery_present) {
        std::cout << std::endl << "Writing SRAM..." << std::endl;
        FILE* fp = fopen(SRAM_path, "wb");
        if (fp == nullptr || (fwrite(nes->cartridge->SRAM, 8192, 1, fp) != 1)) {
            std::cout << "WARN: failed to save SRAM file!" << std::endl;
        }
        else {
            fclose(fp);
        }
    }

    ma_device_uninit(&device);

    return 0;
}
