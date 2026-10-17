// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/Runtime.hpp>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <thread>
#include <vector>

using namespace nomad;

namespace {

class CountingObject final : public NomadObject {
public:
    CountingObject(Runtime& runtime, NomadIndex& destroyed, NomadIndex steps = 1):
        NomadObject(runtime),
        m_destroyed(destroyed),
        m_steps(steps)
    {
    }

    void setChild(CountingObject* child) {
        child->addRef();
        m_children.push_back(child);
    }

protected:
    ~CountingObject() override {
        ++m_destroyed;
    }

    bool reclaimStep() noexcept override {
        if (!m_children.empty()) {
            m_children.back()->releaseRef();
            m_children.pop_back();
            return false;
        }
        return --m_steps == 0;
    }

    void detachReferences() noexcept override {
        for (auto* child : m_children) {
            child->releaseRef();
        }
        m_children.clear();
    }

private:
    NomadIndex& m_destroyed;
    NomadIndex m_steps;
    std::vector<CountingObject*> m_children;
};

} // namespace

BOOST_AUTO_TEST_SUITE(managed_objects)

BOOST_AUTO_TEST_CASE(last_release_enqueues_without_destroying)
{
    Runtime runtime;
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(runtime, destroyed);
    object->addRef();
    BOOST_TEST(object->getReferenceCount() == 2U);
    object->releaseRef();
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}) == 0U);
    object->releaseRef();
    BOOST_TEST(destroyed == 0U);
    BOOST_TEST(runtime.getLiveObjectCount() == 1U);
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}) == 1U);
    BOOST_TEST(destroyed == 1U);
    BOOST_TEST(runtime.getLiveObjectCount() == 0U);
}

BOOST_AUTO_TEST_CASE(reclamation_resumes_with_a_bounded_step_count)
{
    Runtime runtime;
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(runtime, destroyed, 3);
    object->releaseRef();
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}, 1) == 0U);
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}, 1) == 0U);
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}, 1) == 1U);
    BOOST_TEST(destroyed == 1U);
}

BOOST_AUTO_TEST_CASE(child_references_do_not_cause_recursive_destruction)
{
    Runtime runtime;
    NomadIndex destroyed = 0;
    auto* parent = new CountingObject(runtime, destroyed);
    auto* child = new CountingObject(runtime, destroyed);
    parent->setChild(child);
    child->releaseRef();
    parent->releaseRef();
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}, 1) == 0U);
    BOOST_TEST(destroyed == 0U);
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}, 2) == 2U);
    BOOST_TEST(destroyed == 2U);
}

BOOST_AUTO_TEST_CASE(runtime_teardown_reclaims_cycles_and_live_objects)
{
    NomadIndex destroyed = 0;
    {
        Runtime runtime;
        auto* first = new CountingObject(runtime, destroyed);
        auto* second = new CountingObject(runtime, destroyed);
        first->setChild(second);
        second->setChild(first);
        first->releaseRef();
        second->releaseRef();
        BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}) == 0U);
    }
    BOOST_TEST(destroyed == 2U);
}

BOOST_AUTO_TEST_CASE(worker_can_allocate_and_enqueue_before_main_thread_reclamation)
{
    Runtime runtime;
    NomadIndex destroyed = 0;
    {
        auto worker = std::jthread([&] {
            auto* object = new CountingObject(runtime, destroyed);
            object->releaseRef();
        });
    }
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}) == 1U);
    BOOST_TEST(destroyed == 1U);
}

BOOST_AUTO_TEST_CASE(zero_budget_and_zero_steps_leave_work_queued)
{
    Runtime runtime;
    NomadIndex destroyed = 0;
    (new CountingObject(runtime, destroyed))->releaseRef();
    BOOST_TEST(runtime.processReclamation(std::chrono::microseconds{0}) == 0U);
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}, 0) == 0U);
    BOOST_TEST(destroyed == 0U);
    BOOST_TEST(runtime.processReclamation(std::chrono::seconds{1}) == 1U);
}

BOOST_AUTO_TEST_SUITE_END()
