#include <elle/Exception.hh>
#include <elle/log.hh>

#include <elle/reactor/network/Error.hh>
#include <elle/reactor/network/TCPSocket.hh>
#include <elle/reactor/network/TCPServer.hh>
#include <elle/reactor/scheduler.hh>

ELLE_LOG_COMPONENT("LB");

struct Node {
  std::string host;
  std::string port;

  Node(std::string host, std::string port)
    : host{std::move(host)}, port{std::move(port)}
  {}

  auto connect() const
  {
    return std::make_unique<elle::reactor::network::TCPSocket>(host, port);
  }
};

class Nodes {
public:
  using Collection = std::vector<Node>;
  using Iter = Collection::const_iterator;

public:
  Nodes(std::initializer_list<Node> nodes)
    : nodes{nodes}
  {
    assert(not this->nodes.empty());
    iter = this->nodes.cend();
  }

  const Node& next()
  {
    // round robin
    const auto& node = [this]()->const Node&
    {
      if(iter != nodes.cend()) {
        return *iter;
      }
      // reached end, start from beginning
      else {
        iter = Iter{nodes.cbegin()};
        return *iter;
      }
    }();

    ++iter;

    return node;
  }

private:
  const std::vector<Node> nodes;
  Iter iter;
};

struct Connection
  : std::enable_shared_from_this<Connection>
{
  using Socket = elle::reactor::network::TCPSocket;
  using Socket_ptr = std::shared_ptr<Socket>;
  using Thread = elle::reactor::Thread;
  using Thread_ptr = std::unique_ptr<Thread>;
  using Collection = std::unordered_set<std::shared_ptr<Connection>>;

  Collection& collection;
  Socket_ptr outside;
  Socket_ptr inside;

  Thread_ptr out_to_in;
  Thread_ptr in_to_out;

  Connection(Collection& col, Socket_ptr out, Socket_ptr in)
    : collection{col},
      outside{std::move(out)},
      inside{std::move(in)}
  {
    // Classical trick: forward holds a reference to the Connection to maintain
    // it alive while we are not done. The scheduler reset thread actions once
    // they are done, thus losing those reference and deleting properly the
    // threads once they are both done.
    auto forward =
      [this, self = std::shared_ptr<Connection>()]
      (Socket& from, Socket& to) mutable
      {
        self = this->shared_from_this();
        try
        {
          while (true)
          {
            auto payload = from.read_some(4096);
            to.write(payload);
          }
        }
        catch (elle::reactor::network::ConnectionClosed const&)
        {}
        from.close();
        to.close();
        collection.erase(self);
      };
    out_to_in = std::make_unique<Thread>(
      elle::print("{}: out -> in", outside),
      [this, forward] () mutable { forward(*outside, *inside); });
    in_to_out = std::make_unique<Thread>(
      elle::print("{}: in -> out", outside),
      [this, forward] () mutable { forward(*inside, *outside); });
  }

  ~Connection()
  {
    ELLE_LOG("lost connection from {}", this->outside->peer());
  }
};

int
main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
      return 1;
    }
    // Create a Scheduler, the coroutines operator.
    elle::reactor::Scheduler sched;
    // Properly terminate the scheduler in case of SIGINT.
    sched.signal_handle(SIGINT, [&sched] { sched.terminate(); });

    Nodes nodes{
      {"localhost", "8090"},
      {"localhost", "8091"},
      {"localhost", "8092"}
    };

    // Create a coroutine (named elle::reactor::Thread).
    elle::reactor::Thread acceptor(sched, "acceptor", [&]
      {
        elle::reactor::network::TCPServer server;
        auto port = std::atoi(argv[1]);
        server.listen(port);
        Connection::Collection connections;
        while (true)
        {
          // Server::accept yields until it gets a connection.
          auto outside = server.accept();
          // Connect to one of our nodes
          auto inside = nodes.next().connect();
          ELLE_LOG("new connection from {}, forward to {}",
                   outside->peer(), inside->peer());
          connections.emplace(
            std::make_shared<Connection>(
              connections, std::move(outside), std::move(inside)));
        } // < while(true)
      }); // < thread acceptor
    // Run the Scheduler until all coroutines are over or it gets interrupted
    // (by a signal or programmatically).
    sched.run();
  }
  catch (...)
  {
    std::cerr << "fatal error: " << elle::exception_string() << std::endl;
    return 1;
  }
  return 0;
}
