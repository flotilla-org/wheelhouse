// Low-contrast frame-coherence fixture. Every frame must be uniformly grey;
// mixed rows/blocks are a presentation or ownership error, not source content.
#include <assert.h>
#include <jackstay_bootstrap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2 || ft_abi_version() != FT_ABI_VERSION)
    return 2;
  signal(SIGPIPE, SIG_IGN);
  struct sockaddr_un address = {.sun_family = AF_UNIX};
  if (strlen(argv[1]) >= sizeof(address.sun_path))
    return 2;
  strcpy(address.sun_path, argv[1]);
  umask(0077);
  int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0 ||
      bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
      listen(listener, 1)) {
    perror("source socket (must not already exist)");
    return 1;
  }
  enum { WIDTH = 320, HEIGHT = 180, SIZE = WIDTH * HEIGHT * 4 };
  ft_cpu_producer_config config = {
      6, 2, 1, 2, SIZE, 8 * 1024 * 1024, 5000000000ull};
  ft_cpu_producer *producer = 0;
  ft_cpu_setup_server *server = 0;
  ft_input_server *input = 0;
  assert(ft_cpu_producer_create(&config, &producer) == FT_STATUS_OK);
  puts("ready");
  fflush(stdout);
  int fd = accept(listener, 0, 0);
  assert(fd >= 0);
  assert(ft_source_bootstrap_accept(&fd, 0, &input) == FT_STATUS_OK);
  assert(!input);
  assert(ft_cpu_producer_serve(producer, &fd, &server) == FT_STATUS_OK);
  close(listener);
  unlink(argv[1]);
  uint8_t *pixels = malloc(SIZE);
  assert(pixels);
  for (uint64_t sequence = 1;
       ft_cpu_setup_server_poll(server) == FT_STATUS_DRAINING; ++sequence) {
    uint8_t level = sequence % 2 ? 104 : 96;
    for (size_t i = 0; i < SIZE; i += 4) {
      pixels[i] = pixels[i + 1] = pixels[i + 2] = level;
      pixels[i + 3] = 255;
    }
    ft_acquired_frame_descriptor desc = {.sequence = sequence,
                                         .width = WIDTH,
                                         .height = HEIGHT,
                                         .stride = WIDTH * 4,
                                         .pixel_format =
                                             FT_PIXEL_FORMAT_RGBA8_UNORM};
    uint64_t cursor;
    ft_status status =
        ft_cpu_producer_publish(producer, &desc, pixels, SIZE, &cursor);
    assert(status == FT_STATUS_OK || status == FT_STATUS_DROPPED);
    usleep(16000);
  }
  free(pixels);
  ft_cpu_setup_server_destroy(&server);
  for (int i = 0; i < 2000; ++i) {
    ft_status status = ft_cpu_producer_destroy(&producer);
    if (status == FT_STATUS_OK)
      return 0;
    if (status != FT_STATUS_DRAINING)
      return 1;
    usleep(5000);
  }
  fputs("source drain unconfirmed\n", stderr);
  return 1;
}
