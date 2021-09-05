#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/signals2.hpp>
#include <list>
#include <memory>

TEST(SubscriptionTest, ScopedConnection)
{
    boost::signals2::signal<void()> signal;

    auto data = std::make_shared<int>(5);
    const std::weak_ptr<int> data_weak(data);

    {
        // Scoped connection is non copyable/moveable, so the connection will not leave this scope
        const boost::signals2::scoped_connection connection = signal.connect([data = std::move(data)] {});

        // The handler is still alive, it holds the data
        ASSERT_THAT(data_weak.expired(), false);
    }

    // The handler is dead, it does not hold the data any more
    ASSERT_THAT(data_weak.expired(), true);
}

TEST(SubscriptionTest, ConnectionPriority)
{
    boost::signals2::signal<void()> signal;

    std::list<int> data;

    const auto connection10 = signal.connect(1, [&data] {data.push_back(10);});
    const auto connection20 = signal.connect(2, [&data] {data.push_back(20);});
    const auto connection21 = signal.connect(2, [&data] {data.push_back(21);});
    const auto connection11 = signal.connect(1, [&data] {data.push_back(11);});

    signal();

    // Emission is done by the group priority order
    // Within the same group the connection order matters
    ASSERT_THAT(data, testing::ContainerEq(std::list<int>{10,11,20,21}));
}

TEST(SubscriptionTest, ConnectionBlocking)
{
    boost::signals2::signal<void()> signal;

    int data = 5;

    const boost::signals2::connection connection = signal.connect([&data] { ++data; });

    // Connection is active
    signal();
    ASSERT_THAT(data, ::testing::Eq(6));

    {
        // Connection is blocked
        const boost::signals2::shared_connection_block connection_blocker(connection);
        signal();
        ASSERT_THAT(data, ::testing::Eq(6));
    }

    // Connection is active again
    signal();
    ASSERT_THAT(data, ::testing::Eq(7));
}

TEST(SubscriptionTest, SlotLifecycle)
{
    boost::signals2::signal<void()> signal;

    auto data = std::make_shared<int>(5);
    const std::weak_ptr<int> data_weak(data);

    // Data is moved into the slot, it is only owned from there
    const boost::signals2::connection connection = signal.connect([data = std::move(data)] {});
    ASSERT_THAT(data_weak.expired(), false);

    // When slot is disconnected, it is destroyed: we know that, because the owned data is destroyed
    connection.disconnect();
    ASSERT_THAT(data_weak.expired(), true);
}

TEST(SubscriptionTest, SlotLifetimeTracking)
{
    boost::signals2::signal<void()> signal;

    int data = 5;
    struct Slot
    {
        Slot(int &data)
            : data_(data) {}

        void handleSignal()
        {
            ++data_;
        }

        int &data_;
    };
    auto slot = std::make_shared<Slot>(data);

    // If the slot is managed by a shared_ptr, the signal can track the lifetime of the slot.
    // If the slot object is already gone at the time of the emission, then it will be not called back.
    const auto connection = signal.connect(decltype(signal)::slot_type(&Slot::handleSignal, slot.get())
        .track_foreign(slot));

    // Connection is active
    signal();
    ASSERT_THAT(data, ::testing::Eq(6));

    // Destroy the slot, so the signal has a connection with a dead slot
    slot.reset();

    // The dead slot is not called
    signal();
    ASSERT_THAT(data, ::testing::Eq(6));
}

TEST(SubscriptionTest, SignalLifetime)
{
    auto signal = std::make_shared<boost::signals2::signal<void()>>();

    const boost::signals2::connection connection = signal->connect([] {});
    const boost::signals2::connection connection2(connection);

    // Connections are active
    ASSERT_THAT(connection.connected(), true);
    ASSERT_THAT(connection2.connected(), true);

    // Deleting the signal disconnects all its connections
    signal.reset();
    ASSERT_THAT(connection.connected(), false);
    ASSERT_THAT(connection2.connected(), false);
}
