/*
 * nghttp2 - HTTP/2.0 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <iostream>
#include <string>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <nghttp2/nghttp2.h>

#include "app_helper.h"
#include "HttpServer.h"

namespace nghttp2 {

namespace {
int parse_push_config(Config& config, const char *optarg)
{
  const char *eq = strchr(optarg, '=');
  if(eq == NULL) {
    return -1;
  }
  auto paths = std::vector<std::string>();
  auto optarg_end = optarg + strlen(optarg);
  const char *i = eq + 1;
  for(;;) {
    const char *j = strchr(i, ',');
    if(j == NULL) {
      j = optarg_end;
    }
    paths.emplace_back(i, j);
    if(j == optarg_end) {
      break;
    }
    i = j;
    ++i;
  }
  config.push[std::string(optarg, eq)] = std::move(paths);
  return 0;
}
} // namespace

namespace {
void print_usage(std::ostream& out)
{
  out << "Usage: nghttpd [-DVfhv] [-d <PATH>] [--no-tls] <PORT> [<PRIVATE_KEY> <CERT>]"
      << std::endl;
}
} // namespace

namespace {
void print_help(std::ostream& out)
{
  print_usage(out);
  out << "\n"
      << "OPTIONS:\n"
      << "    -D, --daemon       Run in a background. If -D is used, the\n"
      << "                       current working directory is changed to '/'.\n"
      << "                       Therefore if this option is used, -d option\n"
      << "                       must be specified.\n"
      << "    -V, --verify-client\n"
      << "                       The server sends a client certificate\n"
      << "                       request. If the client did not return a\n"
      << "                       certificate, the handshake is terminated.\n"
      << "                       Currently, this option just requests a\n"
      << "                       client certificate and does not verify it.\n"
      << "    -d, --htdocs=<PATH>\n"
      << "                       Specify document root. If this option is\n"
      << "                       not specified, the document root is the\n"
      << "                       current working directory.\n"
      << "    -v, --verbose      Print debug information such as reception/\n"
      << "                       transmission of frames and name/value pairs.\n"
      << "    --no-tls           Disable SSL/TLS.\n"
      << "    -f, --no-flow-control\n"
      << "                       Disables connection and stream level flow\n"
      << "                       controls.\n"
      << "    -c, --header-table-size=<N>\n"
      << "                       Specify decoder header table size.\n"
      << "    --color            Force colored log output.\n"
      << "    -p, --push=<PATH>=<PUSH_PATH,...>\n"
      << "                       Push resources PUSH_PATHs when PATH is\n"
      << "                       requested. This option can be used\n"
      << "                       repeatedly to specify multiple push\n"
      << "                       configurations. For example,\n"
      << "                         -p/=/foo.png -p/doc=/bar.css\n"
      << "                       PATH and PUSH_PATHs are relative to document\n"
      << "                       root. See --htdocs option.\n"
      << "    -h, --help         Print this help.\n"
      << std::endl;
}
} // namespace

int main(int argc, char **argv)
{
  Config config;
  bool color = false;
  while(1) {
    int flag = 0;
    static option long_options[] = {
      {"daemon", no_argument, nullptr, 'D'},
      {"htdocs", required_argument, nullptr, 'd'},
      {"help", no_argument, nullptr, 'h'},
      {"verbose", no_argument, nullptr, 'v'},
      {"verify-client", no_argument, nullptr, 'V'},
      {"no-flow-control", no_argument, nullptr, 'f'},
      {"header-table-size", required_argument, nullptr, 'c'},
      {"push", required_argument, nullptr, 'p'},
      {"no-tls", no_argument, &flag, 1},
      {"color", no_argument, &flag, 2},
      {nullptr, 0, nullptr, 0}
    };
    int option_index = 0;
    int c = getopt_long(argc, argv, "DVc:d:fhp:v", long_options, &option_index);
    char *end;
    if(c == -1) {
      break;
    }
    switch(c) {
    case 'D':
      config.daemon = true;
      break;
    case 'V':
      config.verify_client = true;
      break;
    case 'd':
      config.htdocs = optarg;
      break;
    case 'f':
      config.no_flow_control = true;
      break;
    case 'h':
      print_help(std::cout);
      exit(EXIT_SUCCESS);
    case 'v':
      config.verbose = true;
      break;
    case 'c':
      config.header_table_size = strtol(optarg, &end, 10);
      if(errno == ERANGE || *end != '\0') {
        std::cerr << "-c: Bad option value: " << optarg << std::endl;
        exit(EXIT_FAILURE);
      }
      break;
    case 'p':
      if(parse_push_config(config, optarg) != 0) {
        std::cerr << "-p: Bad option value: " << optarg << std::endl;
      }
      break;
    case '?':
      exit(EXIT_FAILURE);
    case 0:
      switch(flag) {
      case 1:
        // no-tls option
        config.no_tls = true;
        break;
      case 2:
        // color option
        color = true;
        break;
      }
      break;
    default:
      break;
    }
  }
  if(argc - optind < (config.no_tls ? 1 : 3)) {
    print_usage(std::cerr);
    std::cerr << "Too few arguments" << std::endl;
    exit(EXIT_FAILURE);
  }

  config.port = strtol(argv[optind++], nullptr, 10);

  if(!config.no_tls) {
    config.private_key_file = argv[optind++];
    config.cert_file = argv[optind++];
  }

  if(config.daemon) {
    if(config.htdocs.empty()) {
      print_usage(std::cerr);
      std::cerr << "-d option must be specified when -D is used." << std::endl;
      exit(EXIT_FAILURE);
    }
    if(daemon(0, 0) == -1) {
      perror("daemon");
      exit(EXIT_FAILURE);
    }
  }
  if(config.htdocs.empty()) {
    config.htdocs = "./";
  }

  set_color_output(color || isatty(fileno(stdout)));

  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &act, nullptr);
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  SSL_library_init();
  reset_timer();
  config.on_request_recv_callback = htdocs_on_request_recv_callback;

  HttpServer server(&config);
  server.run();
  return 0;
}

} // namespace nghttp2

int main(int argc, char **argv)
{
  return nghttp2::main(argc, argv);
}
