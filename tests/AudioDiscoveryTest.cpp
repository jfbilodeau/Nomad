// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/AudioDiscovery.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <system_error>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(audio_discovery)

BOOST_AUTO_TEST_CASE(discovers_supported_audio_files_and_derives_names)
{
    TestDirectory directory("nomad_audio_discovery_names");
    directory.write("res/audio/hit.wav", "");
    directory.write("res/audio/player/jump.OGG", "");
    directory.write("res/audio/ambient/rain.mp3", "");
    directory.write("res/mod/audio/creatures/slime.flac", "");
    directory.write("res/audio/notes.txt", "");

    const auto sources = discoverAudioSources(
        directory.getPath() / "res",
        {NomadPath("audio"), NomadPath("mod/audio")},
        true
    );

    BOOST_REQUIRE(sources.size() == 4U);
    BOOST_TEST(sources[0].name == "audio.ambient.rain");
    BOOST_TEST(sources[1].name == "audio.hit");
    BOOST_TEST(sources[2].name == "audio.player.jump");
    BOOST_TEST(sources[3].name == "mod.audio.creatures.slime");
}

BOOST_AUTO_TEST_CASE(orders_sources_deterministically)
{
    TestDirectory directory("nomad_audio_discovery_order");
    directory.write("res/audio/zeta.wav", "");
    directory.write("res/audio/Alpha.wav", "");
    directory.write("res/audio/middle.wav", "");

    const auto sources = discoverAudioSources(
        directory.getPath() / "res",
        {NomadPath("audio")},
        true
    );

    BOOST_REQUIRE(sources.size() == 3U);
    BOOST_TEST(sources[0].name == "audio.Alpha");
    BOOST_TEST(sources[1].name == "audio.middle");
    BOOST_TEST(sources[2].name == "audio.zeta");
}

BOOST_AUTO_TEST_CASE(allows_a_missing_implicit_default_directory)
{
    TestDirectory directory("nomad_audio_discovery_default_missing");

    const auto sources = discoverAudioSources(
        directory.getPath() / "res",
        {NomadPath("audio")},
        false
    );

    BOOST_TEST(sources.empty());
}

BOOST_AUTO_TEST_CASE(rejects_a_missing_explicit_directory)
{
    TestDirectory directory("nomad_audio_discovery_explicit_missing");

    BOOST_CHECK_EXCEPTION(
        (void)discoverAudioSources(
            directory.getPath() / "res",
            {NomadPath("audio")},
            true
        ),
        AudioDiscoveryError,
        [](const AudioDiscoveryError& error) {
            return NomadString(error.what()).find("does not exist") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_case_and_extension_name_collisions)
{
    TestDirectory directory("nomad_audio_discovery_collision");
    directory.write("res/audio/hit.mp3", "");
    directory.write("res/audio/Hit.wav", "");

    BOOST_CHECK_EXCEPTION(
        (void)discoverAudioSources(
            directory.getPath() / "res",
            {NomadPath("audio")},
            true
        ),
        AudioDiscoveryError,
        [](const AudioDiscoveryError& error) {
            return NomadString(error.what()).find("name collision") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_collisions_across_configured_roots)
{
    TestDirectory directory("nomad_audio_discovery_root_collision");
    directory.write("res/audio/ui/hit.wav", "");
    directory.write("res/audio.ui/hit.ogg", "");

    BOOST_CHECK_EXCEPTION(
        (void)discoverAudioSources(
            directory.getPath() / "res",
            {NomadPath("audio"), NomadPath("audio.ui")},
            true
        ),
        AudioDiscoveryError,
        [](const AudioDiscoveryError& error) {
            return NomadString(error.what()).find("name collision") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_paths_outside_the_resource_directory)
{
    TestDirectory directory("nomad_audio_discovery_escape");

    BOOST_CHECK_THROW(
        (void)discoverAudioSources(
            directory.getPath() / "res",
            {NomadPath("../audio")},
            true
        ),
        AudioDiscoveryError
    );
}

BOOST_AUTO_TEST_CASE(rejects_symbolic_links_when_supported)
{
    TestDirectory directory("nomad_audio_discovery_symlink");
    directory.write("outside/sound.wav", "");
    std::filesystem::create_directories(directory.getPath() / "res/audio");

    std::error_code error;
    std::filesystem::create_symlink(
        directory.getPath() / "outside/sound.wav",
        directory.getPath() / "res/audio/link.wav",
        error
    );

    if (error) {
        BOOST_TEST_MESSAGE("Symbolic link creation is unavailable: " << error.message());
        return;
    }

    BOOST_CHECK_EXCEPTION(
        (void)discoverAudioSources(
            directory.getPath() / "res",
            {NomadPath("audio")},
            true
        ),
        AudioDiscoveryError,
        [](const AudioDiscoveryError& exception) {
            return NomadString(exception.what()).find("symbolic links") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_SUITE_END()
