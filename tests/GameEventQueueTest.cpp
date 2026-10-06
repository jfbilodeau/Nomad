// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/GameEventQueue.hpp"
#include "nomad/script/Function.hpp"
#include "nomad/script/Type.hpp"

#include <memory>
#include <vector>

using namespace nomad;

namespace {

const NomadString& getEventName(const GameEvent& event) {
    return std::get<GameEventTriggerEvent>(event).eventName;
}

class TrackingType final : public Type {
public:
    explicit TrackingType(bool& valueFreed) : m_valueFreed(valueFreed) {}

    [[nodiscard]] NomadString getTypeName() const override {
        return "tracking";
    }

    void freeValue(RuntimeValue& /*value*/) const override {
        m_valueFreed = true;
    }

    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override {
        destinationValue = sourceValue;
    }

    void toString(const RuntimeValue& /*value*/, NomadString& string) const override {
        string = "tracking";
    }

private:
    bool& m_valueFreed;
};

} // namespace

BOOST_AUTO_TEST_CASE(game_event_queue_processes_only_start_of_frame_events)
{
    GameEventQueue queue;
    std::vector<NomadString> processedNames;

    queue.push(GameEventTriggerEvent{"first"});
    queue.processFrame([&queue, &processedNames](const GameEvent& event) {
        processedNames.push_back(getEventName(event));
        queue.push(GameEventTriggerEvent{"second"});
        return true;
    });

    BOOST_TEST(processedNames == std::vector<NomadString>{"first"});
    BOOST_TEST(queue.size() == 1U);

    queue.processFrame([&processedNames](const GameEvent& event) {
        processedNames.push_back(getEventName(event));
        return true;
    });

    BOOST_TEST(processedNames == std::vector<NomadString>({"first", "second"}));
    BOOST_TEST(queue.empty());
}

BOOST_AUTO_TEST_CASE(game_event_queue_requeues_unprocessed_events)
{
    GameEventQueue queue;
    auto processingAttempts = 0;

    queue.push(GameEventTriggerEvent{"deferred"});
    queue.processFrame([&processingAttempts](const GameEvent& /*event*/) {
        ++processingAttempts;
        return false;
    });

    BOOST_TEST(processingAttempts == 1);
    BOOST_TEST(queue.size() == 1U);

    queue.processFrame([&processingAttempts](const GameEvent& event) {
        ++processingAttempts;
        BOOST_TEST(getEventName(event) == "deferred");
        return true;
    });

    BOOST_TEST(processingAttempts == 2);
    BOOST_TEST(queue.empty());
}

BOOST_AUTO_TEST_CASE(game_event_queue_preserves_fifo_order)
{
    GameEventQueue queue;
    std::vector<NomadString> processedNames;

    queue.push(GameEventTriggerEvent{"first"});
    queue.push(GameEventTriggerEvent{"second"});
    queue.push(GameEventTriggerEvent{"third"});

    queue.processFrame([&processedNames](const GameEvent& event) {
        processedNames.push_back(getEventName(event));
        return true;
    });

    BOOST_TEST(processedNames == std::vector<NomadString>({"first", "second", "third"}));
    BOOST_TEST(queue.empty());
}

BOOST_AUTO_TEST_CASE(game_event_queue_destroys_queued_move_only_payloads)
{
    auto valueFreed = false;
    TrackingType trackingType(valueFreed);
    Function function(0, "function", "function.nomad", "");
    function.createCapture(0, "capture", CaptureSource::Variable, &trackingType);

    {
        GameEventQueue queue;
        std::vector<RuntimeValue> captures{RuntimeValue{0}};
        auto closure = std::make_unique<Closure>(&function, std::move(captures));

        queue.push(GameEventCreateScene{1, "scene", NOMAD_INVALID_ID, std::move(closure)});

        BOOST_TEST(!valueFreed);
        BOOST_TEST(queue.size() == 1U);
    }

    BOOST_TEST(valueFreed);
}
