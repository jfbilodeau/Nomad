// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/log/MemorySink.hpp>

#include <nomad/resource/Audio.hpp>
#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

void appendLittleEndian(std::string& data, const std::uint32_t value, const std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        data.push_back(static_cast<char>((value >> (index * 8)) & 0xff));
    }
}

std::string makeWaveFile() {
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint32_t sampleCount = 800;
    std::string data;
    data.reserve(44 + sampleCount);
    data += "RIFF";
    appendLittleEndian(data, 36 + sampleCount, 4);
    data += "WAVEfmt ";
    appendLittleEndian(data, 16, 4);
    appendLittleEndian(data, 1, 2);
    appendLittleEndian(data, 1, 2);
    appendLittleEndian(data, sampleRate, 4);
    appendLittleEndian(data, sampleRate, 4);
    appendLittleEndian(data, 1, 2);
    appendLittleEndian(data, 8, 2);
    data += "data";
    appendLittleEndian(data, sampleCount, 4);
    data.append(sampleCount, static_cast<char>(128));
    return data;
}

std::unique_ptr<Game> createHeadlessGame(const TestDirectory& directory) {
    GameOptions options;
    options.resourcePath = directory.getPath().string();
    return Game::createHeadless(std::move(options));
}

class ScopedLogCapture final {
public:
    ScopedLogCapture() {
        log::addSink(&m_sink);
    }

    ScopedLogCapture(const ScopedLogCapture&) = delete;
    ScopedLogCapture& operator=(const ScopedLogCapture&) = delete;

    ~ScopedLogCapture() {
        log::removeSink(&m_sink);
    }

    [[nodiscard]] const std::vector<LogEntry>& getEntries() const {
        return m_sink.getEntries();
    }

private:
    MemorySink m_sink;
};

class AudioDriverHintReset final {
public:
    AudioDriverHintReset() = default;
    AudioDriverHintReset(const AudioDriverHintReset&) = delete;
    AudioDriverHintReset& operator=(const AudioDriverHintReset&) = delete;

    ~AudioDriverHintReset() {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(audio)

BOOST_AUTO_TEST_CASE(game_startup_preloads_configured_audio_roots)
{
    TestDirectory directory("nomad_audio_game_preload");
    directory.write("sounds/hit.wav", makeWaveFile());
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write("scripts/init.nomad", "");
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();
    options.audio.directories = std::vector<NomadPath>{"sounds"};
    auto game = Game::create(std::move(options));
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_TEST(audio->getResourceCount() == 1U);
    BOOST_TEST(audio->getResourceByName("sounds.hit") != nullptr);
}

BOOST_AUTO_TEST_CASE(game_scripts_control_preloaded_audio_instances)
{
    TestDirectory directory("nomad_audio_script_api");
    directory.write("audio/hit.wav", makeWaveFile());
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write(
        "scripts/init.nomad",
        "sound = game.audio.load \"audio.hit\"\n"
        "game.volume = 0.8\n"
        "game.audio.volume = 0.5\n"
        "sound.volume = 0.25\n"
        "sound.playLoop\n"
        "sound.pause\n"
        "sound.resume\n"
        "game.audio.pauseAll\n"
        "game.audio.resumeAll\n"
        "game.audio.stopAll\n"
    );
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();
    auto game = Game::create(std::move(options));
    auto* audio = game->getResources()->getAudio();
    const auto* audioType = game->getRuntime()->getTypeByName("audio");

    BOOST_REQUIRE(audioType != nullptr);
    BOOST_TEST(audioType->getDocumentation() == "A controllable instance of a preloaded sound effect.");
    BOOST_TEST(audio->getMasterVolume() == 0.8, boost::test_tools::tolerance(0.001));
    BOOST_TEST(audio->getVolume() == 0.5, boost::test_tools::tolerance(0.001));
    BOOST_REQUIRE(audio->getAudioObjectCount() == 1U);

    const auto* object = audio->getAudioObject(0);
    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(object->getVolume() == 0.25, boost::test_tools::tolerance(0.001));
    BOOST_TEST(!object->isPlaying());
    BOOST_TEST(!object->isPaused());
}

BOOST_AUTO_TEST_CASE(game_scripts_reject_invalid_audio_volume)
{
    TestDirectory directory("nomad_audio_script_volume");
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write("scripts/init.nomad", "game.audio.volume = 1.5\n");
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();

    BOOST_CHECK_EXCEPTION(
        (void)Game::create(std::move(options)),
        GameException,
        [](const GameException& error) {
            return NomadString(error.what()).find("between 0.0 and 1.0") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(game_audio_load_logs_when_playback_is_unavailable)
{
    TestDirectory directory("nomad_audio_script_unavailable");
    directory.write("audio/hit.wav", makeWaveFile());
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write(
        "scripts/init.nomad",
        "sound = game.audio.load \"audio.hit\"\n"
        "game.volume = 0.5\n"
    );
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "nomad-invalid-audio-driver");
    AudioDriverHintReset hintReset;
    ScopedLogCapture logCapture;

    GameOptions options;
    options.resourcePath = directory.getPath().string();
    auto game = Game::create(std::move(options));
    const auto* audio = game->getResources()->getAudio();

    BOOST_TEST(!audio->isAvailable());
    BOOST_TEST(audio->getAudioObjectCount() == 0U);
    BOOST_TEST(audio->getMasterVolume() == 0.5, boost::test_tools::tolerance(0.001));
    BOOST_TEST(std::ranges::any_of(logCapture.getEntries(), [](const LogEntry& entry) {
        return entry.level == LogLevel::Error &&
            entry.message.find("Audio playback is unavailable") != NomadString::npos;
    }));
}

BOOST_AUTO_TEST_CASE(preloads_and_finds_discovered_audio_resources)
{
    TestDirectory directory("nomad_audio_preload");
    directory.write("audio/hit.wav", makeWaveFile());
    directory.write("audio/player/jump.WAV", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_TEST(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 2U);
    BOOST_TEST(audio->getResourceCount() == 2U);

    const auto hitId = audio->getResourceId("audio.hit");
    BOOST_REQUIRE(hitId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(audio->getResource(hitId) != nullptr);
    BOOST_TEST(audio->getResource(hitId)->getName() == "audio.hit");
    BOOST_TEST(audio->getResourceByName("audio.hit") == audio->getResource(hitId));
    BOOST_TEST(audio->getResourceByName("audio.missing") == nullptr);
}

BOOST_AUTO_TEST_CASE(rejects_decode_failures_without_partial_registration)
{
    TestDirectory directory("nomad_audio_preload_failure");
    directory.write("audio/good.wav", makeWaveFile());
    directory.write("audio/invalid.wav", "not a wave file");
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_CHECK_THROW(
        (void)audio->loadAudioRoots(directory.getPath(), {"audio"}, true),
        AudioException
    );
    BOOST_TEST(audio->getResourceCount() == 0U);
}

BOOST_AUTO_TEST_CASE(audio_objects_control_one_reusable_track)
{
    TestDirectory directory("nomad_audio_object");
    directory.write("audio/hit.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 1U);
    const auto objectId = audio->createAudioObject("audio.hit");
    auto* object = audio->getAudioObject(objectId);

    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(audio->getAudioObjectCount() == 1U);
    BOOST_TEST(object->getResource() == audio->getResourceByName("audio.hit"));
    BOOST_TEST(object->getVolume() == 1.0, boost::test_tools::tolerance(0.001));

    object->setVolume(0.5);
    BOOST_TEST(object->getVolume() == 0.5, boost::test_tools::tolerance(0.001));
    BOOST_CHECK_THROW(object->setVolume(-0.1), AudioException);
    BOOST_CHECK_THROW(object->setVolume(1.1), AudioException);

    object->playLoop();
    BOOST_TEST(object->isPlaying());
    BOOST_TEST(object->isLooping());

    object->pause();
    BOOST_TEST(object->isPaused());
    BOOST_TEST(!object->isPlaying());

    object->resume();
    BOOST_TEST(object->isPlaying());

    object->playOnce();
    BOOST_TEST(!object->isLooping());
    BOOST_TEST(audio->getAudioObjectCount() == 1U);

    object->stop();
    BOOST_TEST(!object->isPlaying());
    BOOST_TEST(!object->isPaused());
}

BOOST_AUTO_TEST_CASE(audio_manager_controls_all_tracks_and_master_volume)
{
    TestDirectory directory("nomad_audio_global_controls");
    directory.write("audio/first.wav", makeWaveFile());
    directory.write("audio/second.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 2U);
    auto* first = audio->getAudioObject(audio->createAudioObject("audio.first"));
    auto* second = audio->getAudioObject(audio->createAudioObject("audio.second"));
    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);

    audio->setMasterVolume(0.75);
    BOOST_TEST(audio->getMasterVolume() == 0.75, boost::test_tools::tolerance(0.001));
    BOOST_CHECK_THROW(audio->setMasterVolume(-0.1), AudioException);
    BOOST_CHECK_THROW(audio->setMasterVolume(1.1), AudioException);

    audio->setVolume(0.5);
    BOOST_TEST(audio->getVolume() == 0.5, boost::test_tools::tolerance(0.001));

    first->playLoop();
    second->playLoop();
    audio->pauseAll();
    BOOST_TEST(first->isPaused());
    BOOST_TEST(second->isPaused());

    audio->resumeAll();
    BOOST_TEST(first->isPlaying());
    BOOST_TEST(second->isPlaying());

    audio->stopAll();
    BOOST_TEST(!first->isPlaying());
    BOOST_TEST(!second->isPlaying());
}

BOOST_AUTO_TEST_CASE(rejects_unknown_resources_and_invalid_object_ids)
{
    TestDirectory directory("nomad_audio_invalid_ids");
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_CHECK_THROW((void)audio->createAudioObject("audio.missing"), AudioException);
    BOOST_CHECK_THROW((void)audio->createAudioObject(NOMAD_INVALID_ID), AudioException);
    BOOST_TEST(audio->getAudioObject(NOMAD_INVALID_ID) == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
