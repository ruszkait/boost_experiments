#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/signals2.hpp>
#include <list>
#include <memory>

TEST(SignalTest, ScopedConnection)
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

TEST(SignalTest, ConnectionPriority)
{
    boost::signals2::signal<void()> signal;

    std::list<int> data;

    const auto connection10 = signal.connect(1, [&data] { data.push_back(10); });
    const auto connection20 = signal.connect(2, [&data] { data.push_back(20); });
    const auto connection21 = signal.connect(2, [&data] { data.push_back(21); });
    const auto connection11 = signal.connect(1, [&data] { data.push_back(11); });

    signal();

    // Emission is done by the group priority order
    // Within the same group the connection order matters
    ASSERT_THAT(data, testing::ContainerEq(std::list<int> {10, 11, 20, 21}));
}

TEST(SignalTest, ConnectionBlocking)
{
    boost::signals2::signal<void()> signal;

    int data = 5;

    const boost::signals2::connection connection = signal.connect([&data] { ++data; });

    // Connection is active
    signal();
    ASSERT_THAT(data, ::testing::Eq(6));

    {
        // Scoped connection is blocked
        const boost::signals2::shared_connection_block connection_blocker(connection);
        signal();
        ASSERT_THAT(data, ::testing::Eq(6));
    }

    // Connection is active again
    signal();
    ASSERT_THAT(data, ::testing::Eq(7));
}

TEST(SignalTest, SlotLifecycle)
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

TEST(SignalTest, SlotLifetimeTracking)
{
    boost::signals2::signal<void()> signal;

    int data = 5;
    struct Slot
    {
        Slot(int &data)
            : data_(data)
        { }

        void handleSignal() { ++data_; }

        int &data_;
    };
    auto slot = std::make_shared<Slot>(data);

    // If the slot is managed by a shared_ptr, the signal can track the lifetime of the slot.
    // If the slot object is already gone at the time of the emission, then it will be not called back.
    const auto connection
        = signal.connect(decltype(signal)::slot_type(&Slot::handleSignal, slot.get()).track_foreign(slot));

    // Connection is active
    signal();
    ASSERT_THAT(data, ::testing::Eq(6));

    // Destroy the slot, so the signal has a connection with a dead slot
    slot.reset();

    // The dead slot is not called
    signal();
    ASSERT_THAT(data, ::testing::Eq(6));
}

TEST(SignalTest, SignalLifetime)
{
    auto signal = std::make_shared<boost::signals2::signal<void()> >();

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

TEST(SignalTest, UnsubscribeInEmission)
{
    int callCounter = 0;
    boost::signals2::connection connection;
    boost::signals2::signal<void()> signal;

    // Disconnecting from within the handler -> single shot connection
    connection = signal.connect([&connection, &callCounter] {
        callCounter++;
        connection.disconnect();
    });

    // The first emission has a valid connection
    signal();
    // The second emission has no valid connection
    signal();

    // Connections are active
    ASSERT_THAT(callCounter, testing::Eq(1));
    ASSERT_THAT(connection.connected(), false);
}

#include <latch>
#include <thread>
#include <chrono>
#include <cassert>
#include <condition_variable>

struct TraceCounter
{
    ~TraceCounter()
    {
        std::unique_lock lock(mutex_);
        traceCountReached0Condition_.wait(lock, [this] { return traceCount_ == 0; });
    }

    void RegisterTrace()
    {
        std::lock_guard lock(mutex_);
        traceCount_++;
    }

    void UnRegisterTrace()
    {
        std::lock_guard lock(mutex_);
        traceCount_--;
        if (traceCount_ == 0)
            traceCountReached0Condition_.notify_one();
    }

    std::mutex mutex_;
    std::condition_variable traceCountReached0Condition_;
    std::size_t traceCount_ = 0;
};

class PostEmissionSafeConnection : public boost::signals2::connection
{
public:
    PostEmissionSafeConnection() { }

    PostEmissionSafeConnection(
        boost::signals2::connection &&connection, std::unique_ptr<TraceCounter> counter = nullptr)
        : boost::signals2::connection(std::move(connection))
        , counter_(std::move(counter))
    { }

    PostEmissionSafeConnection(const PostEmissionSafeConnection &) = delete;
    PostEmissionSafeConnection &operator=(const PostEmissionSafeConnection &) = delete;

    PostEmissionSafeConnection(PostEmissionSafeConnection &&other)
        : boost::signals2::connection(std::move(other))
        , counter_(std::move(other.counter_))
    { }

    PostEmissionSafeConnection &operator=(PostEmissionSafeConnection &&other)
    {
        boost::signals2::connection::operator=(std::move(other));
        counter_ = std::move(other.counter_);

        return *this;
    }

    ~PostEmissionSafeConnection() { Release(); }

    void Release()
    {
        disconnect();
        counter_.reset();
    }

private:
    std::unique_ptr<TraceCounter> counter_;
};

struct Tracer
{
    Tracer() = delete;

    Tracer(TraceCounter *counter)
        : counter_(counter)
    {
        counter_->RegisterTrace();
    }

    Tracer(const Tracer &other)
        : counter_(other.counter_)
    {
        counter_->RegisterTrace();
    }

    Tracer &operator=(const Tracer &other)
    {
        counter_ = other.counter_;
        counter_->RegisterTrace();
        return *this;
    }

    Tracer(Tracer &&other)
        : counter_(other.counter_)
    {
        other.counter_ = nullptr;
    }

    Tracer &operator=(Tracer &&other)
    {
        counter_ = other.counter_;
        other.counter_ = nullptr;
        return *this;
    }

    ~Tracer()
    {
        if (counter_)
            counter_->UnRegisterTrace();
    }

    TraceCounter *counter_;
};

class BatteryController
{
public:
    PostEmissionSafeConnection OnBatteryLow(
        const std::function<void()> &handler, std::unique_ptr<TraceCounter> trackerCounter = {})
    {
        if (!trackerCounter)
        {
            return batteryLowSignal_.connect(handler);
        }

        auto connection = batteryLowSignal_.connect([tracer = Tracer(trackerCounter.get()), handler] { handler(); });
        return {std::move(connection), std::move(trackerCounter)};
    }

    void NotifyBatteryLow() { batteryLowSignal_(); }

private:
    boost::signals2::signal<void()> batteryLowSignal_;
};

TEST(SignalTest, PostUnsubscriptionEmission)
{
    BatteryController batteryController;

    std::latch emissionBegin(1);
    std::latch handlerDisconnected(1);
    std::latch emissionFinished(1);

    PostEmissionSafeConnection connection = batteryController.OnBatteryLow(
        [&emissionBegin, &handlerDisconnected] {
            emissionBegin.count_down();
            handlerDisconnected.wait();
        },
        std::make_unique<TraceCounter>());

    std::jthread signalSenderThread([&batteryController, &emissionFinished] {
        batteryController.NotifyBatteryLow();
        emissionFinished.count_down();
    });

    // At this point we wait for a running emission.
    emissionBegin.wait();

    // Disconnect during emission
    // It releases the signal slots, except the one that is held by the emission
    connection.disconnect();
    handlerDisconnected.count_down();

    // Block as long as the emission is running and holding the last slot
    connection.Release();
    ASSERT_THAT(emissionFinished.try_wait(), true);
}
