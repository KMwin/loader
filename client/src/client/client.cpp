#include "../include.h"
#include "client.h"

void tcp::client::start(const std::string_view server_ip, const uint16_t port) {
  SSL_library_init();

  m_ssl_ctx = SSL_CTX_new(TLS_client_method());

  m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (m_socket == -1) {
    io::logger->error("failed to create socket.");
    return;
  }

  sockaddr_in server_addr;

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(server_ip.data());
  server_addr.sin_port = htons(port);

  int ret = connect(m_socket, reinterpret_cast<sockaddr*>(&server_addr),
                    sizeof(server_addr));
  if (ret < 0) {
    io::logger->error("failed to connect to server.");
    return;
  }

  m_server_ssl = SSL_new(m_ssl_ctx);
  SSL_set_fd(m_server_ssl, m_socket);

  ret = SSL_connect(m_server_ssl);

  if (ret != 1) {
    ret = SSL_get_error(m_server_ssl, ret);
    io::logger->error("ssl connection failed, code {}", ret);
    return;
  }

  m_active = true;

  io::logger->info("connected.");
}

int tcp::client::read_stream(std::vector<char>& out) {
  size_t size;
  read(&size, sizeof(size));

  size = ntohl(size);
  out.resize(size);

  constexpr size_t chunk_size = 4096;
  size_t total = 0;

  while (size > 0) {
    auto to_read = std::min(size, chunk_size);
    std::string chunk;
    chunk.resize(to_read + 2);
    int ret = read(&chunk[0], to_read + 2);
    if (ret <= 0) {
      break;
    }
    enc::decrypt_message(chunk);
    std::memcpy(&out[total], chunk.data(), chunk.size());

    size -= ret - 2;
    total += ret - 2;
  }

  return total;
}

int tcp::client::stream(std::vector<char>& data) {
  auto size = data.size();

  auto networked_size = htonl(size);
  write(&networked_size, sizeof(networked_size));

  // with 4kb chunk size, speed peaks at 90mb/s
  constexpr size_t chunk_size = 4096;
  size_t sent = 0;

  while (size > 0) {
    auto to_send = std::min(size, chunk_size);

    int ret = write(&data[sent], to_send);
    if (ret <= 0) {
      break;
    }

    sent += ret;
    size -= ret;
  }

  return sent;
}
