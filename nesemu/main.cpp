#include <cmath>
#include <vector>
#include <chrono>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/canvas.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/color.hpp"

#include "NES.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

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

    // input
    uint8_t controller1 = 0;

    using namespace ftxui;

    std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> prev_time{std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())};
    auto board_renderer = CatchEvent(Renderer([&] {
        std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds> time{std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())};
        const double dt = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(time - prev_time).count()) / 1000.0f;
        prev_time = time;

        // processe input
        nes->controller1->buttons = controller1;
        nes->controller2->buttons = 0;

        // step the NES state forward by 'dt' seconds, or more if in fast-forward
        emulate(nes, dt);

        Elements array;
        int x_length = nes_width;
        int y_length = nes_height;

        for (int y = 0; y < y_length; y += 2) {
            Elements line;
            for (int x = 0; x < x_length; ++x) {
                uint32_t c = nes->ppu->front[y * nes_width +  x];
                auto const blue = static_cast<uint8_t>(c >> 16U);
                auto const green = static_cast<uint8_t>(c >> 8U);
                auto const red = static_cast<uint8_t>(c);

                uint32_t c2 = nes->ppu->front[(y+1) * nes_width +  x];
                auto const blue2 = static_cast<uint8_t>(c2 >> 16U);
                auto const green2 = static_cast<uint8_t>(c2 >> 8U);
                auto const red2 = static_cast<uint8_t>(c2);

//                line.push_back(text(L"▀") | color(Color::RGB(red, green, blue)) | bgcolor(Color::RGB(red2, green2, blue2)));
                line.push_back(text(L"▀") | color(Color(red, green, blue)) | bgcolor(Color(red2, green2, blue2)));
            }
            array.push_back(hbox(std::move(line)));
        }

        return window(text("SimpleNES"), vbox(array));
    }), [&](const Event &e) {
        uint8_t ret = e.is_character() && "z" == e.character(); // A
        ret |= (e.is_character() && "x" == e.character()) << 1; // B
        ret |= (e == Event::Backspace) << 2;                    // Select
        ret |= (e == Event::Return) << 3;                       // Start
        ret |= (e == Event::ArrowUp) << 4;
        ret |= (e == Event::ArrowDown) << 5;
        ret |= (e == Event::ArrowLeft) << 6;
        ret |= (e == Event::ArrowRight) << 7;
        controller1 = ret;
        return false;
    });

    auto screen = ScreenInteractive::FitComponent();
    std::atomic<bool> refresh_ui_continue = true;
    std::thread refresh_ui([&] {
        while (refresh_ui_continue) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1.0s / 60.0);// NOLINT magic numbers
            screen.PostEvent(Event::Custom);
        }
    });
    screen.Loop(board_renderer);
    refresh_ui_continue = false;
    refresh_ui.join();

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
