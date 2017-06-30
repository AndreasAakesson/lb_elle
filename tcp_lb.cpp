/*
  Create a TCP echo server, which echo you back what you sent.

  How to run:
  $ ./reactor/examples/echo_server 8080

  How to test (using netcat):
  $ nc 127.0.0.1 8080
*/
#include <elle/With.hh>
#include <elle/utility/Move.hh>
#include <elle/Exception.hh>

#include <elle/reactor/Scope.hh>
#include <elle/reactor/network/Error.hh>
#include <elle/reactor/network/TCPSocket.hh>
#include <elle/reactor/network/TCPServer.hh>
#include <elle/reactor/scheduler.hh>

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

struct Connection {
  using Socket = elle::reactor::network::TCPSocket;

  std::unique_ptr<Socket> outside;
  std::unique_ptr<Socket> inside;

  ~Connection() {
    std::cout << "~Connection" << std::endl;
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

    Nodes nodes{{"localhost", "8090"}, {"localhost", "8091"}};

    // Create a coroutine (named elle::reactor::Thread).
    elle::reactor::Thread acceptor(sched, "acceptor", [&]
      {
        elle::reactor::network::TCPServer server;
        auto port = std::atoi(argv[1]);
        server.listen(port);
        // Scope enable to start tasks and make sure they are terminated upon
        // destruction, elle::With handles nested exceptions.
        elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& scope)
        {
          while (true)
          {
            try
            {
              auto conn = std::make_shared<Connection>();
              // Server::accept yields until it gets a connection.
              conn->outside = elle::utility::move_on_copy(server.accept());
              // Connect to one of our nodes
              conn->inside = nodes.next().connect();

              // Write Outside -> Inside
              scope.run_background(
                elle::sprintf("client_out %s", conn),
                [conn]
                {
                  try
                  {
                    while (true)
                    {
                      auto payload = conn->outside->read(4096);
                      //conn->inside->write(payload);
                    }
                  }
                  catch (elle::reactor::network::ConnectionClosed const&)
                  {
                    std::cout << "Connection closed in client_out" << std::endl;
                    throw;
                  }
                }); // < scope out -> in

              // Write Inside -> Outside
              scope.run_background(
                elle::sprintf("client_in %s", conn),
                [conn]
                {
                  try
                  {
                    while (true)
                    {
                      auto payload = conn->inside->read(4096);
                      conn->outside->write(payload);
                    }
                  }
                  catch (elle::reactor::network::ConnectionClosed const&)
                  {
                    std::cout << "Connection closed in client_in" << std::endl;
                    throw;
                  }
                }); // < scope in -> out
            }
            catch (elle::reactor::network::ConnectionClosed const&)
            {
              std::cout << "Connection closed" << std::endl;
            }
          } // < while(true)
          scope.wait();
        }; // < scope
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
