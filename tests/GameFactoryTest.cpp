// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>
#include <nomad/game/GameFactory.hpp>

#include <boost/program_options/errors.hpp>
#include <boost/test/unit_test.hpp>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(game_factory)

BOOST_AUTO_TEST_CASE(runtime_options_accept_debug_and_resource_path)
{
    char program[] = "nomad-runtime";
    char resourcePathOption[] = "--resource-path";
    char resourcePath[] = "project-resources";
    char debugOption[] = "--debug";
    char* arguments[] = { program, resourcePathOption, resourcePath, debugOption };
    GameOptions options;

    parseCommandLine(4, arguments, &options);

    BOOST_TEST(options.resourcePath == "project-resources");
    BOOST_TEST(options.debug);
}

BOOST_AUTO_TEST_CASE(runtime_options_reject_removed_generation_flags)
{
    char program[] = "nomad-runtime";
    char documentationOption[] = "--doc";
    char* documentationArguments[] = { program, documentationOption };
    GameOptions documentationOptions;

    BOOST_CHECK_THROW(
        parseCommandLine(2, documentationArguments, &documentationOptions),
        boost::program_options::unknown_option
    );

    char grammarOption[] = "--tm";
    char* grammarArguments[] = { program, grammarOption };
    GameOptions grammarOptions;

    BOOST_CHECK_THROW(
        parseCommandLine(2, grammarArguments, &grammarOptions),
        boost::program_options::unknown_option
    );
}

BOOST_AUTO_TEST_SUITE_END()
