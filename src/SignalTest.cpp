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
#include <future>
#include <thread>
#include <chrono>
#include <cassert>

class PostEmissionSafeConnection : public boost::signals2::scoped_connection
{
public:
    class HandlerTrace
    {
    public:
        HandlerTrace()
            : connection_(nullptr)
        { }

        HandlerTrace(PostEmissionSafeConnection &connection)
            : connection_(&connection)
        {
            connection_->Register();
        }

        HandlerTrace(const HandlerTrace &other)
            : connection_(other.connection_)
        {
            connection_->Register();
        }

        HandlerTrace &operator=(const HandlerTrace &other)
        {
            connection_ = other.connection_;
            connection_->Register();
            return *this;
        }

        HandlerTrace(HandlerTrace &&other)
            : connection_(other.connection_)
        {
            other.connection_ = nullptr;
        }

        HandlerTrace &operator=(HandlerTrace &&other)
        {
            connection_ = other.connection_;
            other.connection_ = nullptr;
            return *this;
        }

        ~HandlerTrace()
        {
            if (connection_)
                connection_->UnRegister();
        }

    private:
        PostEmissionSafeConnection *connection_ = nullptr;
    };

    PostEmissionSafeConnection() { Register(); }

    PostEmissionSafeConnection(const boost::signals2::connection &connection)
        : boost::signals2::scoped_connection(connection)
    {
        Register();
    }

    PostEmissionSafeConnection &operator=(const boost::signals2::connection &connection)
    {
        boost::signals2::scoped_connection::operator=(connection);
        return *this;
    }

    ~PostEmissionSafeConnection()
    {
        UnRegister();
        disconnect();
        noMoreTraceFuture_.wait();
    }

    HandlerTrace IssueTrace() { return HandlerTrace(*this); }

    std::shared_future<void> WHenNoMoreB() { return noMoreTraceFuture_; }

private:
    friend HandlerTrace;

    void Register()
    {
        for (;;)
        {
            auto currentTraceCount = traceCount_.load();

            auto incrementedTraceCount = currentTraceCount + 1;

            const bool exchanged = traceCount_.compare_exchange_strong(
                currentTraceCount, incrementedTraceCount, std::memory_order_release, std::memory_order_relaxed);

            if (exchanged)
                break;
        }
    }

    void UnRegister()
    {
        for (;;)
        {
            auto currentTraceCount = traceCount_.load();
            assert(currentTraceCount > 0);

            auto decrementedTraceCount = currentTraceCount - 1;

            const bool exchanged = traceCount_.compare_exchange_strong(
                currentTraceCount, decrementedTraceCount, std::memory_order_release, std::memory_order_relaxed);

            if (!exchanged)
                continue;

            const bool noMoreTrace = decrementedTraceCount == 0;

            if (noMoreTrace)
                noMoreTracePromise_.set_value();

            break;
        }
    }

    std::promise<void> noMoreTracePromise_;
    std::shared_future<void> noMoreTraceFuture_ = noMoreTracePromise_.get_future().share();
    std::atomic<std::size_t> traceCount_ = 0;
};

// FIXME. because of the pointer the HandlerTrace, the connection is not copyable or movable
template <typename TSignal, typename THandler>
PostEmissionSafeConnection connect_signal(TSignal &signal, const THandler &handler)
{
    PostEmissionSafeConnection connection;
    auto trackedHandler = [handlerTrace = connection.IssueTrace(), handler] { handler(); };
    connection = signal.connect(trackedHandler);
    return connection;
}

TEST(SignalTest, PostUnsubscriptionEmission)
{
    // The handler must outlive all traces
    boost::signals2::signal<void()> signal;

    PostEmissionSafeConnection connection;
    connection = signal.connect([handlerTrace = connection.IssueTrace()] {
        using namespace std::literals;
        std::this_thread::sleep_for(1s);
    });

    auto emissionDone = std::async([&signal] { signal(); });

    // the connection blocks its destruction until all handler instances are destroyed.
}
