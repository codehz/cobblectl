#define API_MODE 1
#include <CLI/CLI.hpp>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <rpcws.hpp>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include "utils.hpp"

using namespace rpcws;

auto ep = std::make_shared<epoll>();
bool debug_mode;

RPC::Client &nsgod() {
  static RPC::Client client{std::make_unique<client_wsio>("ws+unix://.cobblestone/nsgod.socket", ep)};
  return client;
}

RPC::Client &server_instance(std::string const &name = "") {
  static RPC::Client client{std::make_unique<client_wsio>("ws+unix://" + name + "/api.socket", ep)};
  return client;
}

template <typename F> void handle_fail(F f) {
  try {
    f();
  } catch (std::exception &ex) {
    std::cerr << ex.what() << std::endl;
    ep->shutdown();
    exit(EXIT_FAILURE);
  }
}

template <> void handle_fail<std::exception_ptr>(std::exception_ptr e) {
  if (e)
    handle_fail([&] { std::rethrow_exception(e); });
}

template <typename T, T... ch> std::enable_if_t<std::is_same_v<T, char>, std::string &> operator""_str() {
  static std::string var;
  return var;
}

template <typename T, T... ch> std::enable_if_t<std::is_same_v<T, char>, bool &> operator""_flag() {
  static bool var;
  return var;
}

template <typename T, T... ch> std::enable_if_t<std::is_same_v<T, char>, std::vector<std::string> &> operator""_vstr() {
  static std::vector<std::string> var;
  return var;
}

const auto service_name_validator = CLI::Validator(
    [](std::string &input) -> std::string {
      if (input.size() == 0 || input.find_first_of('.') != std::string::npos)
        return "invalid name";
      return {};
    },
    "PROFILE", "profile name");

typedef enum { MODLOADER_LOG_TRACE, MODLOADER_LOG_DEBUG, MODLOADER_LOG_INFO, MODLOADER_LOG_WARN, MODLOADER_LOG_ERROR } modloader_log_level;

const char *modloader_log_level_str(modloader_log_level level) {
  if (level == MODLOADER_LOG_TRACE)
    return "T";
  if (level == MODLOADER_LOG_DEBUG)
    return "D";
  if (level == MODLOADER_LOG_INFO)
    return "I";
  if (level == MODLOADER_LOG_WARN)
    return "W";
  if (level == MODLOADER_LOG_ERROR)
    return "E";
  return "?";
}

namespace fs = std::filesystem;

int main(int argc, char **argv) {
  using namespace std::chrono;
  CLI::App app{"cobblestone manager"};
  app.set_help_all_flag("--help-all");
  app.require_subcommand(-1);
  app.require_subcommand(1);
  auto check = app.add_subcommand("check", "check current installation");
  check->callback([] {
    fs::path base{".cobblestone"};
    if (!fs::is_directory(base)) {
      std::cerr << "Not installed at all" << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!fs::is_regular_file(base / "nsgod")) {
      std::cerr << "nsgod (process manager) is not installed" << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!fs::is_directory(base / "core") || !fs::is_regular_file(base / "core" / "run" / "stone")) {
      std::cerr << "StoneServer core is not installed" << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!fs::is_directory(base / "game") || !fs::is_regular_file(base / "game" / "bedrock_server")) {
      std::cerr << "Minecraft (bedrock edition) is not installed" << std::endl;
      exit(EXIT_FAILURE);
    }
    std::cout << "Seems all components is installed" << std::endl;
  });
  auto start = app.add_subcommand("start", "start service");
  start->add_option("service", "start-service"_str, "target service to start")->required()->check(CLI::ExistingDirectory & service_name_validator);
  start->add_flag("--wait", "start-wait"_flag, "wait for started");
  start->preparse_callback(start_nsgod);
  start->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] {
            ProcessLaunchOptions options{
                .waitstop = true,
                .pty = true,
                .root = fs::absolute(".cobblestone/core"),
                .cwd = "/run",
                .log = fs::absolute(fs::path("start-service"_str) / "stone.log"),
                .cmdline = {"./game/bedrock_server"},
                .env =
                    {
                        "UPSTART_JOB=cobblestone",
                        "LD_PRELOAD=/run/loader.so",
                        "HOME=/run/data",
                    },
                .mounts =
                    {
                        {"run/game", fs::absolute(".cobblestone/game")},
                        {"run/data", fs::absolute("start-service"_str)},
                        {"dev", "/dev"},
                        {"sys", "/sys"},
                        {"proc", "/proc"},
                        {"tmp", "/tmp"},
                    },
                .restart =
                    RestartPolicy{
                        .enabled = true,
                        .max = 5,
                        .reset_timer = 1min,
                    },
            };
            if ("start-wait"_flag) {
              nsgod().on("output", [](json data) {
                if (data["service"] == "start-service"_str)
                  std::cout << data["data"].get<std::string>();
              });
              nsgod().on("started", [](json data) {
                if (data["service"] == "start-service"_str) {
                  std::cout << "start-service"_str
                            << " started" << std::endl;
                  ep->shutdown();
                }
              });
            }
            return nsgod().call("start", json::object({
                                             {"service", "start-service"_str},
                                             {"options", options},
                                         }));
          })
          .then([](json ret) {
            std::cout << "start-service"_str
                      << " launched" << std::endl;
            if (!"start-wait"_flag)
              ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto ps = app.add_subcommand("ps", "list running services");
  ps->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] { return nsgod().call("status", json::object({})); })
          .then([](json ret) {
            for (auto [k, v] : ret.items()) {
              std::cout << k << "\t" << v["status"] << std::endl;
            }
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto dump = app.add_subcommand("dump", "dump service stack");
  dump->add_option("service", "dump-service"_str, "target service to dump")->required()->check(CLI::ExistingDirectory & service_name_validator);
  dump->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<void>>([] {
            nsgod().on("output", [](json data) {
              if (data["service"] == "dump-service"_str) {
                std::cout << data["data"].get<std::string>() << std::flush;
              }
            });
            return nsgod()
                .call("kill", json::object({
                                  {"service", "dump-service"_str},
                                  {"signal", SIGUSR1},
                                  {"restart", 0},
                              }))
                .then<void>([](json ret) {});
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto stop = app.add_subcommand("stop", "kill service(s)");
  stop->add_option("service", "stop-service"_vstr, "target service(s) to stop")->required()->check(CLI::ExistingDirectory & service_name_validator)->expected(-1);
  stop->add_flag("--restart,!--no-restart", "stop-restart"_flag, "restart service after killed");
  stop->add_flag("--force", "stop-force"_flag, "force stop service(SIGKILL)");
  stop->add_flag("--wait", "stop-wait"_flag, "wait for stopped");
  stop->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<void>>([] {
            return promise<void>::map_all(std::vector<std::string>{"stop-service"_vstr}, [](std::string const &input) -> promise<void> {
              return nsgod().call("status", json::object({{"service", input}})).then<void>([](json ret) {});
            });
          })
          .then<promise<void>>([] {
            return promise<void>::map_all(std::vector<std::string>{"stop-service"_vstr}, [](std::string const &input) -> promise<void> {
              if ("stop-wait"_flag)
                nsgod().on("stopped", [](json data) {
                  static size_t killed = 0;
                  if (std::find("stop-service"_vstr.begin(), "stop-service"_vstr.end(), data["service"]) != "stop-service"_vstr.end()) {
                    killed++;
                    std::cout << data["service"].get<std::string>() << " stopped" << std::endl;
                  }
                  if (killed == "stop-service"_vstr.size())
                    ep->shutdown();
                });
              return nsgod()
                  .call("kill", json::object({
                                    {"service", input},
                                    {"signal", SIGTERM},
                                    {"restart", "stop-restart"_flag ? 1 : -1},
                                }))
                  .then<void>([](json ret) {});
            });
          })
          .then([] {
            std::cout << "sent SIGTERM signal to "
                      << "stop-service"_vstr.size() << " service(s)" << std::endl;
            if (!"stop-wait"_flag)
              ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto ping = app.add_subcommand("ping-daemon", "ping daemon");
  ping->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] { return nsgod().call("ping", json::object({})); })
          .then([](auto) {
            std::cout << "daemon is running" << std::endl;
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto kill = app.add_subcommand("kill-daemon", "stop all services and kill the daemon");
  kill->callback([] {
    handle_fail([] {
      nsgod()
          .start()
          .then<promise<json>>([] { return nsgod().call("shutdown", json::object({})); })
          .then([](auto) {
            std::cout << "daemon is shutdown" << std::endl;
            ep->shutdown();
          })
          .fail(handle_fail<std::exception_ptr>);
    });
    ep->wait();
  });
  auto attach = app.add_subcommand("attach", "attach to service's command interface");
  attach->add_flag("--wait", "attach-wait"_flag, "wait for command (deperated)");
  attach->add_option("--sender", "attach-executor"_str, "sender name");
  attach->add_option("service", "attach-service"_str, "target service name")->required()->check(CLI::ExistingDirectory & service_name_validator);
  attach->callback([] {
    handle_fail([] {
      server_instance("attach-service"_str).start().then([] {
        static auto prompt = ("attach-executor"_str.empty() ? "" : ("attach-executor"_str + "@")) + "attach-service"_str + "> ";
        static int wait = 0;
        static bool done = false;
        static constexpr auto clear_line = [] {
          if (isatty(0))
            std::cout << "\33[2K\r" << std::flush;
        };
        static constexpr auto show_prompt = [] {
          if (isatty(0))
            std::cout << prompt << std::flush;
        };
        server_instance()
            .on("core.log",
                [](json data) {
                  clear_line();
                  std::cerr << modloader_log_level_str(data["level"]) << " [" << data["tag"].get<std::string>() << "] " << data["content"].get<std::string>() << std::endl;
                  show_prompt();
                })
            .fail(handle_fail<std::exception_ptr>);
        server_instance()
            .on("chat.recv",
                [](json data) {
                  clear_line();
                  std::cerr << "<" << data["sender"].get<std::string>() << "> " << data["content"].get<std::string>() << std::endl;
                  show_prompt();
                })
            .fail(handle_fail<std::exception_ptr>);
        std::thread([] {
          std::string line;
          while (true) {
            clear_line();
            show_prompt();
            if (!std::getline(std::cin, line)) {
              done = true;
              if (wait) {
                std::cerr << "waiting for command result..." << std::endl;
                return;
              }
              ep->shutdown();
              return;
            }
            if (line.empty())
              continue;
            wait++;
            if (line[0] == '/')
              server_instance()
                  .call("command.execute", json::object({
                                               {"name", "attach-executor"_str},
                                               {"command", line},
                                           }))
                  .then([](json data) {
                    clear_line();
                    if (data["statusMessage"].is_string())
                      std::cout << data["statusMessage"].get<std::string>() << std::endl;
                    show_prompt();
                    if (--wait == 0 && !isatty(0) && done)
                      ep->shutdown();
                    return;
                  })
                  .fail(handle_fail<std::exception_ptr>);
            else
              server_instance()
                  .call("chat.send", json::object({
                                         {"sender", "attach-executor"_str},
                                         {"content", line},
                                     }))
                  .then([](json data) {
                    clear_line();
                    std::cout << "sent" << std::endl;
                    show_prompt();
                    if (--wait == 0 && !isatty(0) && done)
                      ep->shutdown();
                    return;
                  })
                  .fail(handle_fail<std::exception_ptr>);
            ;
          }
        }).detach();
      });
      ep->wait();
    });
  });
  CLI11_PARSE(app, argc, argv);
}