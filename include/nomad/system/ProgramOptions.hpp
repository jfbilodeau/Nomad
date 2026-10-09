// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <boost/program_options.hpp>

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nomad {

struct EmptyParameters {};

namespace cli {

class Command {
public:
    Command(std::string name, std::string description)
        : m_name(std::move(name)), m_description(std::move(description)) {}
    virtual ~Command() = default;
    virtual int run(std::span<const std::string> arguments, std::ostream& output, const std::string& program) const = 0;
    virtual void printHelp(std::ostream& output, const std::string& program) const = 0;

    [[nodiscard]] const std::string& getName() const { return m_name; }
    [[nodiscard]] const std::string& getDescription() const { return m_description; }

private:
    std::string m_name;
    std::string m_description;
};

inline void reserveName(std::vector<std::string>& names, const std::string& name, const std::string& alias) {
    const auto valid = [](const std::string& value) {
        return !value.empty() && value.front() != '-' && value.find_first_of(" ,=\t\r\n") == std::string::npos;
    };
    if (!valid(name) || (!alias.empty() && (!valid(alias) || alias.size() != 1))) {
        throw std::invalid_argument("Invalid command-line name or alias: " + name);
    }
    for (const auto& existing : names) {
        if (existing == name || (!alias.empty() && existing == alias)) {
            throw std::invalid_argument("Duplicate command-line name or alias: " + existing);
        }
    }
    if (name == alias) {
        throw std::invalid_argument("Command-line name and alias must differ");
    }
    names.push_back(name);
    if (!alias.empty()) {
        names.push_back(alias);
    }
}

} // namespace cli

template<typename Parameters>
class VerbOptions final : public cli::Command {
public:
    VerbOptions(std::string name, std::function<int(Parameters&)> handler, std::string description)
        : Command(std::move(name), std::move(description)), m_handler(std::move(handler)) {}

    VerbOptions& addFlag(
        const std::string& name, const std::string& alias,
        bool Parameters::* member, const std::string& description
    ) {
        cli::reserveName(m_names, name, alias);
        m_bindings.emplace_back([=](auto& options, Parameters& parameters) {
            const auto key = alias.empty() ? name : name + "," + alias;
            options.add_options()(key.c_str(),
                boost::program_options::bool_switch(&(parameters.*member)), description.c_str());
        });
        return *this;
    }

    template<typename Value>
    VerbOptions& addOption(
        const std::string& name, const std::string& alias,
        std::optional<Value> Parameters::* member, const std::string& description
    ) {
        cli::reserveName(m_names, name, alias);
        m_bindings.emplace_back([=](auto& options, Parameters& parameters) {
            const auto key = alias.empty() ? name : name + "," + alias;
            options.add_options()(key.c_str(),
                boost::program_options::value<Value>()->notifier([&parameters, member](const Value& value) {
                    parameters.*member = value;
                }), description.c_str());
        });
        return *this;
    }

    template<typename Value>
    VerbOptions& addRequiredOption(
        const std::string& name, const std::string& alias,
        Value Parameters::* member, const std::string& description
    ) {
        cli::reserveName(m_names, name, alias);
        m_bindings.emplace_back([=](auto& options, Parameters& parameters) {
            const auto key = alias.empty() ? name : name + "," + alias;
            options.add_options()(key.c_str(),
                boost::program_options::value<Value>(&(parameters.*member))->required(), description.c_str());
        });
        return *this;
    }

    template<typename Value>
    VerbOptions& addOptionalPositional(
        const std::string& name, std::optional<Value> Parameters::* member, const std::string& description
    ) {
        addOption(name, "", member, description);
        addPositional(name, false);
        return *this;
    }

    template<typename Value>
    VerbOptions& addRequiredPositional(
        const std::string& name, Value Parameters::* member, const std::string& description
    ) {
        if (m_optionalPositional) {
            throw std::invalid_argument("Required positionals must precede optional positionals");
        }
        addRequiredOption(name, "", member, description);
        addPositional(name, true);
        return *this;
    }

    int run(std::span<const std::string> arguments, std::ostream& output, const std::string& program) const override {
        namespace po = boost::program_options;
        Parameters parameters{};
        po::options_description options("Options");
        bind(options, parameters);
        po::variables_map values;
        const std::vector<std::string> tokens(arguments.begin(), arguments.end());
        po::store(po::command_line_parser(tokens).options(options).positional(m_positionals)
            .style(po::command_line_style::default_style & ~po::command_line_style::allow_guessing).run(), values);
        if (values.contains("help")) {
            printHelp(output, program);
            return EXIT_SUCCESS;
        }
        po::notify(values);
        return m_handler(parameters);
    }

    void printHelp(std::ostream& output, const std::string& program) const override {
        Parameters parameters{};
        boost::program_options::options_description options("Options");
        bind(options, parameters);
        output << "Usage: " << program << ' ' << getName() << m_usage << " [options]\n\n"
               << getDescription() << "\n\n" << options << '\n';
    }

private:
    void bind(boost::program_options::options_description& options, Parameters& parameters) const {
        options.add_options()("help,h", "Show help for this command");
        for (const auto& binding : m_bindings) {
            binding(options, parameters);
        }
    }

    void addPositional(const std::string& name, const bool required) {
        m_positionals.add(name.c_str(), 1);
        m_usage += required ? " <" + name + ">" : " [" + name + "]";
        m_optionalPositional = m_optionalPositional || !required;
    }

    std::function<int(Parameters&)> m_handler;
    std::vector<std::function<void(boost::program_options::options_description&, Parameters&)>> m_bindings;
    boost::program_options::positional_options_description m_positionals;
    std::vector<std::string> m_names{"help", "h"};
    std::string m_usage;
    bool m_optionalPositional = false;
};

class ProgramOptions {
public:
    ProgramOptions(std::string name, std::string description)
        : m_name(std::move(name)), m_description(std::move(description)) {}

    ProgramOptions(const ProgramOptions&) = delete;
    ProgramOptions& operator=(const ProgramOptions&) = delete;
    ProgramOptions(ProgramOptions&&) = delete;
    ProgramOptions& operator=(ProgramOptions&&) = delete;

    ProgramOptions& addGlobal(
        const std::string& name, const std::string& alias,
        std::function<int()> handler, const std::string& description
    ) {
        cli::reserveName(m_globalNames, name, alias);
        m_globals.push_back({name, alias, std::move(handler), description});
        return *this;
    }

    template<typename Parameters>
    VerbOptions<Parameters>& addVerb(
        const std::string& name, std::function<int(Parameters&)> handler, const std::string& description
    ) {
        cli::reserveName(m_verbNames, name, "");
        auto command = std::make_unique<VerbOptions<Parameters>>(name, std::move(handler), description);
        auto& result = *command;
        m_commands.push_back(std::move(command));
        return result;
    }

    int run(int argc, char** argv, std::ostream& output) const {
        std::vector<std::string> arguments;
        for (auto i = 1; i < argc; ++i) {
            arguments.emplace_back(argv[i]);
        }
        return run(arguments, output);
    }

    int run(std::span<const std::string> arguments, std::ostream& output) const {
        if (arguments.empty()) {
            printHelp(output);
            return EXIT_FAILURE;
        }
        for (const auto& global : m_globals) {
            if (arguments.front() == "--" + global.name ||
                (!global.alias.empty() && arguments.front() == "-" + global.alias)) {
                if (arguments.size() != 1) {
                    throw boost::program_options::error("The --" + global.name + " option must stand alone");
                }
                return global.handler();
            }
        }
        for (const auto& command : m_commands) {
            if (command->getName() == arguments.front()) {
                return command->run(arguments.subspan(1), output, m_name);
            }
        }
        throw boost::program_options::error("Unknown command or global option: " + arguments.front());
    }

    void printHelp(std::ostream& output, const std::optional<std::string>& verb = std::nullopt) const {
        if (verb) {
            for (const auto& command : m_commands) {
                if (command->getName() == *verb) {
                    command->printHelp(output, m_name);
                    return;
                }
            }
            throw boost::program_options::error("Unknown command: " + *verb);
        }
        output << "Usage: " << m_name << " <verb> [arguments] [options]\n\n"
               << m_description << "\n\nCommands:\n";
        for (const auto& command : m_commands) {
            output << "  " << command->getName() << "  " << command->getDescription() << '\n';
        }
        output << "\nStandalone options:\n";
        for (const auto& global : m_globals) {
            output << "  --" << global.name;
            if (!global.alias.empty()) {
                output << ", -" << global.alias;
            }
            output << "  " << global.description << '\n';
        }
        output << "\nUse " << m_name << " <verb> --help for command help.\n";
    }

private:
    struct Global {
        std::string name;
        std::string alias;
        std::function<int()> handler;
        std::string description;
    };

    std::string m_name;
    std::string m_description;
    std::vector<Global> m_globals;
    std::vector<std::unique_ptr<cli::Command>> m_commands;
    std::vector<std::string> m_globalNames;
    std::vector<std::string> m_verbNames;
};

} // namespace nomad
