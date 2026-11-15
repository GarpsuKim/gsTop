#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <iomanip>
#include <algorithm>

class WAVPlayer {
private:
    ma_engine engine{};
    ma_sound sound{};

    bool engineReady = false;
    bool soundReady = false;

    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<float> volume{1.0f};

    ma_uint64 lengthFrames = 0;
    ma_uint32 sampleRate = 0;

public:
    bool initialize()
    {
        ma_result result = ma_engine_init(nullptr, &engine);

        if (result != MA_SUCCESS) {
            std::cout << "오디오 엔진 초기화 실패: "
                      << result << "\n";
            return false;
        }

        engineReady = true;
        return true;
    }

    bool load(const std::string& filename)
    {
        if (!engineReady) return false;

        ma_result result = ma_sound_init_from_file(
            &engine,
            filename.c_str(),
            MA_SOUND_FLAG_STREAM,
            nullptr,
            nullptr,
            &sound
        );

        if (result != MA_SUCCESS) {
            std::cout << "WAV 파일 로드 실패: "
                      << result << "\n";
            return false;
        }

        soundReady = true;

        ma_sound_get_length_in_pcm_frames(
            &sound, &lengthFrames
        );

        sampleRate = ma_engine_get_sample_rate(&engine);

        ma_sound_set_volume(&sound, volume.load());

        return true;
    }

    void play()
    {
        if (!soundReady) return;

        if (ma_sound_start(&sound) == MA_SUCCESS) {
            paused = false;
            playing = true;
            // std::cout << "재생 시작\n";
        }
    }

    void pause()
    {
        if (!soundReady || !playing) return;

        ma_sound_stop(&sound);   // 정지하지만 위치는 유지 = 일시정지
        paused = true;

        std::cout << "일시정지\n";
    }

    void resume()
    {
        if (!soundReady || !paused) return;

        ma_sound_start(&sound);  // 현재 위치에서 재개
        paused = false;

        std::cout << "재생 재개\n";
    }

    void toggle()
    {
        if (!soundReady) return;

        if (paused) {
            resume();
        }
        else if (playing) {
            pause();
        }
        else {
            play();
        }
    }

    void stop()
    {
        if (!soundReady) return;

        ma_sound_stop(&sound);
        ma_sound_seek_to_pcm_frame(&sound, 0);

        playing = false;
        paused = false;

        std::cout << "정지 완료\n";
    }

    void setVolume(float value)
    {
        value = std::clamp(value, 0.0f, 100.0f);

        volume = value / 100.0f;

        if (soundReady)
            ma_sound_set_volume(&sound, volume.load());

        std::cout << "볼륨: "
                  << value << "%\n";
    }

    void showTime()
    {
        if (!soundReady) return;

        ma_uint64 position = 0;

        ma_sound_get_cursor_in_pcm_frames(
            &sound, &position
        );

        double current = sampleRate
            ? static_cast<double>(position) / sampleRate
            : 0;

        double total = sampleRate
            ? static_cast<double>(lengthFrames) / sampleRate
            : 0;

        auto formatTime = [](double seconds) {
            int minutes = static_cast<int>(seconds) / 60;
            int secs = static_cast<int>(seconds) % 60;

            std::cout << std::setfill('0')
                      << std::setw(2) << minutes << ":"
                      << std::setw(2) << secs;
        };

        std::cout << "재생 시간: ";

        formatTime(current);
        std::cout << " / ";
        formatTime(total);

        std::cout << "\n";

        if (lengthFrames > 0) {
            double percent =
                100.0 * position / lengthFrames;

            std::cout << "진행률: "
                      << std::fixed
                      << std::setprecision(1)
                      << percent << "%\n";
        }
    }

    void update()
    {
        if (!soundReady || !playing || paused)
            return;

        ma_uint64 position = 0;

        ma_sound_get_cursor_in_pcm_frames(
            &sound, &position
        );

        if (lengthFrames > 0 &&
            position >= lengthFrames) {
            playing = false;
            paused = false;
            // std::cout << "\n재생 완료!\n";
        }
    }

    bool finished()
    {
        return !soundReady || ma_sound_at_end(&sound);
    }

    void shutdown()
    {
        if (soundReady) {
            ma_sound_uninit(&sound);
            soundReady = false;
        }

        if (engineReady) {
            ma_engine_uninit(&engine);
            engineReady = false;
        }
    }

    ~WAVPlayer()
    {
        shutdown();
    }
};

int main(int argc, char* argv[])
{
    WAVPlayer player;

    std::string filename;
/*
    std::cout
        << "================================\n"
        << "       C++ WAV PLAYER\n"
        << "================================\n\n";
*/
    if (!player.initialize()) {
        return 1;
    }

    bool autoPlay = (argc > 1);

    if (autoPlay) {
        filename = argv[1];
    }
    else {
        std::cout << "WAV 파일 경로: ";
        std::getline(std::cin, filename);
    }

    if (filename.empty()) {
        std::cout << "파일 경로가 비어 있습니다.\n";
        return 1;
    }

    if (!player.load(filename)) {
        return 1;
    }

    // std::cout << "\n파일 로드 성공!\n";

    if (autoPlay) {
        player.play();

        while (!player.finished()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        player.shutdown();

        // std::cout << "\n재생 완료! 종료합니다.\n";

        return 0;
    }

    char command;

    while (true)
    {
        player.update();

        std::cout
            << "\n========== MENU ==========\n"
            << "P : 재생 / 일시정지\n"
            << "S : 정지\n"
            << "V : 볼륨 조절\n"
            << "T : 재생 시간\n"
            << "Q : 종료\n"
            << "==========================\n"
            << "명령 입력: ";

        std::cin >> command;

        if (command == 'P' || command == 'p') {
            player.toggle();
        }
        else if (command == 'S' || command == 's') {
            player.stop();
        }
        else if (command == 'V' || command == 'v') {
            float value;

            std::cout << "볼륨 (0~100): ";
            std::cin >> value;

            player.setVolume(value);
        }
        else if (command == 'T' || command == 't') {
            player.showTime();
        }
        else if (command == 'Q' || command == 'q') {
            break;
        }
        else {
            std::cout << "잘못된 명령입니다.\n";
        }
    }

    player.shutdown();

    // std::cout << "\nWAV Player 종료\n";

    return 0;
}