// Independent instrumented source for native view input tests. The first byte
// on stdin releases admission; later bytes request an execution-state report.
#include <assert.h>
#include <jackstay_handshake.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
static void ok(ft_status status)
{
  assert(status == FT_STATUS_OK);
}
int main(int argc, char **argv)
{
  assert(argc == 2 && ft_abi_version() == FT_ABI_VERSION);
  signal(SIGPIPE, SIG_IGN);
  umask(0077);
  setbuf(stdout, NULL);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  assert(strlen(argv[1]) < sizeof(addr.sun_path));
  strcpy(addr.sun_path, argv[1]);
  int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(listener >= 0 &&
         !bind(listener, (struct sockaddr *)&addr, sizeof(addr)) &&
         !listen(listener, 1));
  enum
  {
    W = 320,
    H = 180,
    N = W * H * 4
  };
  ft_cpu_producer_config mc = {6, 2, 1, 2, N, 8 * 1024 * 1024, 5000000000ull};
  ft_cpu_producer *producer = NULL;
  ft_cpu_setup_server *media = NULL;
  ft_input_config ic;
  ft_input_config_default(&ic);
  ic.independent_contributions = 1;
  ic.interaction_cancel = 1;
  ft_input_target *target = NULL;
  ft_input_server *input = NULL;
  ok(ft_cpu_producer_create(&mc, &producer));
  ok(ft_input_target_create(&ic, &target));
  puts("ready");
  int fd = accept(listener, NULL, NULL);
  assert(fd >= 0);
  puts("accepted");
  assert(getchar() != EOF);
  // The v3 track handshake: video, and input this source authorizes.
  ft_local_connection *connection = NULL;
  ft_track_offer tracks[2] = {
      {1, FT_TRACK_KIND_VIDEO, "default", NULL, 0, FT_TRACK_FROM_SOURCE, 0, {{0}, 0, 0}},
      {2, FT_TRACK_KIND_INPUT, "default", NULL, 0, FT_TRACK_TO_SOURCE, 0, {{0}, 0, 0}}};
  ft_track_admission admissions[2];
  size_t admitted = 0;
  ft_affordances_producer *affordances = NULL;
  ok(ft_local_connection_adopt(&fd, &connection));
  ok(ft_handshake_accept_local(&connection, "native input fixture", FT_CLOCK_DOMAIN_UNKNOWN, tracks, 2,
                               NULL, 0, target, admissions, &admitted, &input, &affordances, NULL));
  ok(ft_cpu_producer_serve_local(producer, &connection, &media));
  close(listener);
  unlink(argv[1]);
  puts("connected");
  // Wire fields for both state and snapshot: downs ups buttons held cleanup keys text.
  unsigned downs = 0, ups = 0, buttons = 0, held = 0, cleanup = 0, keys = 0,
           text = 0;
  int done = 0;
  unsigned char pixels[N];
  for (int i = 0; i < N; i += 4)
  {
    pixels[i] = 40;
    pixels[i + 1] = 70;
    pixels[i + 2] = 95;
    pixels[i + 3] = 255;
  }
  for (uint64_t sequence = 1; !done; ++sequence)
  {
    ft_input_work *work = NULL;
    while (ft_input_target_next(target, &work) == FT_STATUS_OK)
    {
      ft_input_operation op;
      ok(ft_input_work_describe(work, &op));
      ft_input_event *e = &op.event;
      if (e->kind == FT_INPUT_BUTTON)
      {
        if (e->action == FT_INPUT_DOWN)
        {
          downs++;
          buttons |= 1u << e->button;
        }
        else
        {
          ups++;
          buttons &= ~(1u << e->button);
        }
      }
      if (e->kind == FT_INPUT_KEY)
      {
        if (e->action == FT_INPUT_DOWN)
        {
          held++;
          keys++;
        }
        else if (e->action == FT_INPUT_UP && held)
          held--;
      }
      if (e->kind == FT_INPUT_TEXT)
        text += (unsigned)e->text_len;
      if (e->kind == FT_INPUT_CLEANUP)
      {
        buttons = 0;
        if (op.scope == FT_INPUT_SCOPE_ALL)
          held = 0;
        cleanup++;
        if (op.reason != FT_INPUT_REASON_FOCUS &&
            op.reason != FT_INPUT_REASON_GEOMETRY)
          done = 1;
      }
      ok(ft_input_work_complete(&work, FT_INPUT_EXECUTED));
      printf("state %u %u %u %u %u %u %u\n", downs, ups, buttons, held, cleanup,
             keys, text);
    }
    struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
    if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN))
    {
      char c;
      if (read(0, &c, 1) == 1)
        printf("snapshot %u %u %u %u %u %u %u\n", downs, ups, buttons, held,
               cleanup, keys, text);
    }
    ft_acquired_frame_descriptor desc = {.sequence = sequence,
                                         .width = W,
                                         .height = H,
                                         .stride = W * 4,
                                         .pixel_format =
                                             FT_PIXEL_FORMAT_RGBA8_UNORM};
    uint64_t cursor;
    ft_status status =
        ft_cpu_producer_publish(producer, &desc, pixels, N, &cursor);
    assert(status == FT_STATUS_OK || status == FT_STATUS_DROPPED);
    usleep(16000);
  }
  for (int i = 0; i < 2000 && ft_input_server_poll(input) == FT_STATUS_EMPTY;
       i++)
    usleep(1000);
  ft_input_server_destroy(&input);
  ok(ft_input_target_destroy(&target));
  ft_cpu_setup_server_destroy(&media);
  for (int i = 0; i < 2000; i++)
  {
    ft_status s = ft_cpu_producer_destroy(&producer);
    if (s == FT_STATUS_OK)
      return 0;
    assert(s == FT_STATUS_DRAINING);
    usleep(5000);
  }
  return 1;
}
