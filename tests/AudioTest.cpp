// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/game/resource/Audio.hpp>
#include <nomad/game/resource/AudioObject.hpp>
#include <nomad/game/resource/ResourceManager.hpp>

#include <nomad/log/MemorySink.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/SystemFunctions.hpp>

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

std::unique_ptr<Game> createAudioGame(Runtime& runtime, GameOptions options) {
    registerSystemFunctions(&runtime);
    auto game = Game::createForRuntime(&runtime, options, false);
    const std::vector<NomadPath> paths{pathFromUtf8(options.resourcePath) / "scripts"};
    game->prepareScripts([&] {
        const auto result = checkPaths(paths, &runtime);
        if (!result.succeeded()) {
            NomadString message = "Failed to compile audio test scripts";
            for (const auto& diagnostic : result.diagnostics) {
                message += "\n" + formatDiagnostic(diagnostic);
            }
            throw GameException(message);
        }
    });
    runtime.initializeScripts();
    game->executeEntry();
    game->onEntryExecuted();
    return game;
}

template<typename Predicate>
bool waitForAudioCallback(AudioManager* audio, Predicate&& predicate) {
    constexpr auto timeoutMilliseconds = 1000U;
    constexpr auto pollMilliseconds = 10U;

    for (auto elapsed = 0U; elapsed < timeoutMilliseconds; elapsed += pollMilliseconds) {
        SDL_Delay(pollMilliseconds);
        audio->processCallbacks();

        if (predicate()) {
            return true;
        }
    }

    return false;
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
    directory.write("scripts/main.nomad", "");
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();
    options.audio.directories = std::vector<NomadPath>{"sounds"};
    Runtime runtime;
    auto game = createAudioGame(runtime, std::move(options));
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
        "scripts/main.nomad",
        "fun audioFinished\n"
        " game.volume = 0.7\n"
        "end\n"
        "fun adjust sound:Audio\n"
        " sound.volume = (toFloat \"a\".length) / 4.0\n"
        "end\n"
        "sound = game.audio.load \"audio.hit\"\n"
        "alias = sound\n"
        "game.volume = 0.8\n"
        "game.audio.volume = 0.5\n"
        "adjust sound\n"
        "assert alias.volume == 0.25\n"
        "sound.playLoop\n"
        "sound.playOnceThen audioFinished\n"
        "sound.pause\n"
        "sound.resume\n"
        "game.audio.pauseAll\n"
        "game.audio.resumeAll\n"
        "game.audio.stopAll\n"
        "assert sound.volume == 0.25\n"
        "assert !sound.playing\n"
        "assert !sound.paused\n"
    );
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();
    Runtime runtime;
    auto game = createAudioGame(runtime, std::move(options));
    auto* audio = game->getResources()->getAudio();
    const auto* audioType = game->getRuntime()->getTypeByName("Audio");

    BOOST_REQUIRE(audioType != nullptr);
    BOOST_TEST(
        audioType->getDocumentation() ==
        "A sound effect that can be played, paused, resumed, stopped, looped, and adjusted independently."
    );
    BOOST_TEST(audio->getMasterVolume() == 0.8, boost::test_tools::tolerance(0.001));
    BOOST_TEST(audio->getVolume() == 0.5, boost::test_tools::tolerance(0.001));
    BOOST_TEST(audio->getAudioObjectCount() == 0U);
}

BOOST_AUTO_TEST_CASE(game_scripts_control_streamed_music)
{
    TestDirectory directory("nomad_music_script_api");
    directory.write("music/theme.wav", makeWaveFile());
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write(
        "scripts/main.nomad",
        "fun musicFinished\n"
        " game.volume = 0.7\n"
        "end\n"
        "game.music.volume = 0.4\n"
        "game.audio.pauseAutomatically = false\n"
        "game.music.pauseAutomatically = false\n"
        "game.music.play \"music.theme\"\n"
        "game.music.pause\n"
        "game.music.resume\n"
        "game.music.playOnce \"music.theme\"\n"
        "game.music.playThen \"music.theme\" musicFinished\n"
        "game.music.stop\n"
    );
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();
    Runtime runtime;
    auto game = createAudioGame(runtime, std::move(options));
    const auto* audio = game->getResources()->getAudio();

    BOOST_TEST(audio->getMusicVolume() == 0.4, boost::test_tools::tolerance(0.001));
    BOOST_TEST(!audio->getAudioPauseAutomatically());
    BOOST_TEST(!audio->getMusicPauseAutomatically());
    BOOST_TEST(!audio->isMusicPlaying());
    BOOST_TEST(!audio->isMusicPaused());
}

BOOST_AUTO_TEST_CASE(game_scripts_log_invalid_audio_volume)
{
    TestDirectory directory("nomad_audio_script_volume");
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write("scripts/main.nomad", "game.audio.volume = 1.5\n");
    std::filesystem::create_directories(directory.getPath() / "mods");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    GameOptions options;
    options.resourcePath = directory.getPath().string();

    Runtime runtime;
    auto game = createAudioGame(runtime, std::move(options));
    BOOST_TEST(game->getResources()->getAudio()->getVolume() == 1.0);
}

BOOST_AUTO_TEST_CASE(game_audio_load_faults_when_playback_is_unavailable)
{
    TestDirectory directory("nomad_audio_script_unavailable");
    directory.write("audio/hit.wav", makeWaveFile());
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    directory.write(
        "scripts/main.nomad",
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
    Runtime runtime;
    BOOST_CHECK_EXCEPTION(
        (void)createAudioGame(runtime, std::move(options)),
        GameException,
        [](const GameException& error) {
            return NomadString(error.what()).find("Audio playback is unavailable") != NomadString::npos;
        }
    );
    BOOST_TEST(std::ranges::any_of(logCapture.getEntries(), [](const LogEntry& entry) {
        return entry.level == LogLevel::Warning &&
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
    BOOST_TEST(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 0U);
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
    auto* object = audio->createAudioObject("audio.hit");
    const auto releaseObject = [](AudioObject* value) { if (value != nullptr) { value->releaseRef(); } };
    const std::unique_ptr<AudioObject, decltype(releaseObject)> ownedObject(object, releaseObject);

    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(audio->getAudioObjectCount() == 1U);
    BOOST_TEST(object->getResource() == audio->getResourceByName("audio.hit"));
    BOOST_TEST(object->getVolume() == 1.0, boost::test_tools::tolerance(0.001));

    object->setVolume(0.5);
    BOOST_TEST(object->getVolume() == 0.5, boost::test_tools::tolerance(0.001));
    object->setVolume(-0.1);
    BOOST_TEST(object->getVolume() == 0.0, boost::test_tools::tolerance(0.001));
    object->setVolume(1.1);
    BOOST_TEST(object->getVolume() == 1.0, boost::test_tools::tolerance(0.001));

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

BOOST_AUTO_TEST_CASE(audio_last_reference_unregisters_and_cancels_playback)
{
    TestDirectory directory("nomad_audio_reference_lifetime");
    directory.write("audio/hit.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();
    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 1U);
    auto* object = audio->createAudioObject("audio.hit");
    BOOST_REQUIRE(object != nullptr);
    bool called = false;
    object->playOnceThen([&] { called = true; });
    addRef(object);
    releaseRef(object);
    BOOST_TEST(audio->getAudioObjectCount() == 1U);
    releaseRef(object);
    BOOST_TEST(audio->getAudioObjectCount() == 0U);
    audio->pauseAll();
    audio->resumeAll();
    audio->stopAll();
    audio->pauseAutomatically();
    audio->resumeAutomatically();
    audio->processCallbacks();
    BOOST_TEST(!called);
}

BOOST_AUTO_TEST_CASE(audio_reference_can_be_released_after_manager_shutdown)
{
    TestDirectory directory("nomad_audio_manager_shutdown");
    directory.write("audio/hit.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();
    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 1U);
    auto* object = audio->createAudioObject("audio.hit");
    BOOST_REQUIRE(object != nullptr);
    object->playLoop();
    game.reset();
    BOOST_TEST(object->getResource() == nullptr);
    BOOST_TEST(!object->isPlaying());
    releaseRef(object);
}

BOOST_AUTO_TEST_CASE(audio_registry_tracks_only_surviving_instances)
{
    TestDirectory directory("nomad_audio_live_registry");
    directory.write("audio/hit.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();
    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 1U);
    auto* first = audio->createAudioObject("audio.hit");
    auto* second = audio->createAudioObject("audio.hit");
    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);
    BOOST_TEST(first != second);
    releaseRef(first);
    BOOST_TEST(audio->getAudioObjectCount() == 1U);
    second->playLoop();
    audio->pauseAll();
    BOOST_TEST(second->isPaused());
    audio->resumeAll();
    BOOST_TEST(second->isPlaying());
    releaseRef(second);
    BOOST_TEST(audio->getAudioObjectCount() == 0U);
    for (auto index = 0; index < 32; ++index) {
        auto* object = audio->createAudioObject("audio.hit");
        BOOST_REQUIRE(object != nullptr);
        releaseRef(object);
    }
    BOOST_TEST(audio->getAudioObjectCount() == 0U);
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
    auto* first = audio->createAudioObject("audio.first");
    const auto releaseFirst = [](AudioObject* value) { if (value != nullptr) { value->releaseRef(); } };
    const std::unique_ptr<AudioObject, decltype(releaseFirst)> ownedFirst(first, releaseFirst);
    auto* second = audio->createAudioObject("audio.second");
    const auto releaseSecond = [](AudioObject* value) { if (value != nullptr) { value->releaseRef(); } };
    const std::unique_ptr<AudioObject, decltype(releaseSecond)> ownedSecond(second, releaseSecond);
    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);

    audio->setMasterVolume(0.75);
    BOOST_TEST(audio->getMasterVolume() == 0.75, boost::test_tools::tolerance(0.001));
    audio->setMasterVolume(-0.1);
    BOOST_TEST(audio->getMasterVolume() == 0.0, boost::test_tools::tolerance(0.001));
    audio->setMasterVolume(1.1);
    BOOST_TEST(audio->getMasterVolume() == 1.0, boost::test_tools::tolerance(0.001));

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

BOOST_AUTO_TEST_CASE(audio_manager_streams_music_on_a_separate_track)
{
    TestDirectory directory("nomad_music_stream");
    directory.write("music/theme.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_REQUIRE(audio->playMusic(directory.getPath(), "music.theme", true));
    BOOST_TEST(audio->isMusicPlaying());
    BOOST_TEST(audio->isMusicLooping());
    SDL_Delay(150);
    audio->processCallbacks();
    BOOST_TEST(audio->isMusicPlaying());

    audio->pauseAll();
    BOOST_TEST(audio->isMusicPlaying());

    audio->pauseMusic();
    BOOST_TEST(audio->isMusicPaused());
    BOOST_TEST(!audio->isMusicPlaying());

    audio->resumeMusic();
    BOOST_TEST(audio->isMusicPlaying());

    audio->setMusicVolume(0.25);
    BOOST_TEST(audio->getMusicVolume() == 0.25, boost::test_tools::tolerance(0.001));
    audio->setMusicVolume(-0.1);
    BOOST_TEST(audio->getMusicVolume() == 0.0, boost::test_tools::tolerance(0.001));
    audio->setMusicVolume(1.1);
    BOOST_TEST(audio->getMusicVolume() == 1.0, boost::test_tools::tolerance(0.001));

    BOOST_REQUIRE(audio->playMusic(directory.getPath(), "music.theme", false));
    BOOST_TEST(audio->isMusicPlaying());
    BOOST_TEST(!audio->isMusicLooping());

    audio->stopMusic();
    BOOST_TEST(!audio->isMusicPlaying());
    BOOST_TEST(!audio->isMusicPaused());
    BOOST_TEST(!audio->playMusic(directory.getPath(), "music.missing", true));
}

BOOST_AUTO_TEST_CASE(audio_completion_callbacks_run_after_playback_and_cancel_on_stop)
{
    TestDirectory directory("nomad_audio_completion");
    directory.write("audio/hit.wav", makeWaveFile());
    directory.write("music/theme.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 1U);
    auto* object = audio->createAudioObject("audio.hit");
    const auto releaseObject = [](AudioObject* value) { if (value != nullptr) { value->releaseRef(); } };
    const std::unique_ptr<AudioObject, decltype(releaseObject)> ownedObject(object, releaseObject);
    BOOST_REQUIRE(object != nullptr);

    auto soundCompleted = false;
    object->playOnceThen([&soundCompleted] {
        soundCompleted = true;
    });
    BOOST_TEST(waitForAudioCallback(audio, [&soundCompleted] {
        return soundCompleted;
    }));

    auto canceledSoundCompleted = false;
    object->playOnceThen([&canceledSoundCompleted] {
        canceledSoundCompleted = true;
    });
    object->stop();
    SDL_Delay(150);
    audio->processCallbacks();
    BOOST_TEST(!canceledSoundCompleted);

    auto musicCompleted = false;
    BOOST_REQUIRE(audio->playMusic(
        directory.getPath(),
        "music.theme",
        false,
        [&musicCompleted] {
            musicCompleted = true;
        }
    ));
    BOOST_TEST(waitForAudioCallback(audio, [&musicCompleted] {
        return musicCompleted;
    }));

    auto canceledMusicCompleted = false;
    BOOST_REQUIRE(audio->playMusic(
        directory.getPath(),
        "music.theme",
        false,
        [&canceledMusicCompleted] {
            canceledMusicCompleted = true;
        }
    ));
    audio->stopMusic();
    SDL_Delay(150);
    audio->processCallbacks();
    BOOST_TEST(!canceledMusicCompleted);
}

BOOST_AUTO_TEST_CASE(game_pause_automatically_controls_configured_audio_categories)
{
    TestDirectory directory("nomad_audio_automatic_pause");
    directory.write("audio/hit.wav", makeWaveFile());
    directory.write("music/theme.wav", makeWaveFile());
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->loadAudioRoots(directory.getPath(), {"audio"}, true) == 1U);
    auto* object = audio->createAudioObject("audio.hit");
    const auto releaseObject = [](AudioObject* value) { if (value != nullptr) { value->releaseRef(); } };
    const std::unique_ptr<AudioObject, decltype(releaseObject)> ownedObject(object, releaseObject);
    BOOST_REQUIRE(object != nullptr);
    object->playLoop();
    BOOST_REQUIRE(audio->playMusic(directory.getPath(), "music.theme", true));

    game->pause();
    BOOST_TEST(object->isPaused());
    BOOST_TEST(audio->isMusicPaused());

    game->resume();
    BOOST_TEST(object->isPlaying());
    BOOST_TEST(audio->isMusicPlaying());

    audio->setAudioPauseAutomatically(false);
    audio->setMusicPauseAutomatically(false);
    game->pause();
    BOOST_TEST(object->isPlaying());
    BOOST_TEST(audio->isMusicPlaying());
    game->resume();

    object->pause();
    audio->setAudioPauseAutomatically(true);
    game->pause();
    game->resume();
    BOOST_TEST(object->isPaused());
}

BOOST_AUTO_TEST_CASE(rejects_unknown_resources_and_invalid_object_ids)
{
    TestDirectory directory("nomad_audio_invalid_ids");
    auto game = createHeadlessGame(directory);
    auto* audio = game->getResources()->getAudio();

    BOOST_REQUIRE(audio->isAvailable());
    BOOST_TEST(audio->createAudioObject("audio.missing") == nullptr);
    BOOST_TEST(audio->createAudioObject(NOMAD_INVALID_ID) == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
