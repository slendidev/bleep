#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libvinput.h"

#include <spa/param/latency-utils.h>
#include <spa/pod/builder.h>

#include <pipewire/filter.h>
#include <pipewire/pipewire.h>

#include <pthread.h>

#define M_PI_M2 (M_PI + M_PI)

#define DEFAULT_RATE 44100
#define DEFAULT_FREQ 440
#define DEFAULT_VOLUME 0.6

_Atomic(bool) g_bleep = false;

void on_key(KeyboardEvent ev) {
  if (!ev.pressed) {
    g_bleep = false;
    return;
  }
  if (!ev.modifiers.right_control)
    return;
  g_bleep = true;
}

struct data;

struct port {
  struct data *data;
  double accumulator;
};

struct data {
  struct pw_main_loop *loop;
  struct pw_filter *filter;
  struct port *in_port;
  struct port *out_port;
};

static void on_process(void *userdata, struct spa_io_position *position) {
  struct data *d = userdata;
  float *in, *out;
  uint32_t n_samples = position->clock.duration;
  uint32_t i;

  in = pw_filter_get_dsp_buffer(d->in_port, n_samples);
  out = pw_filter_get_dsp_buffer(d->out_port, n_samples);

  if (!in || !out)
    return;

  // Generate continuous sine wave if g_bleep is true, else pass input through.
  for (i = 0; i < n_samples; i++) {
    if (g_bleep) {
      d->out_port->accumulator += M_PI_M2 * DEFAULT_FREQ / DEFAULT_RATE;
      if (d->out_port->accumulator >= M_PI_M2)
        d->out_port->accumulator -= M_PI_M2;

      out[i] = sin(d->out_port->accumulator) * DEFAULT_VOLUME;
    } else {
      out[i] = in[i];
    }
  }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .process = on_process,
};

static void do_quit(void *userdata, int signal_number) {
  struct data *d = userdata;
  pw_main_loop_quit(d->loop);
}

void *thread_proc_pw(void *user_data) {
  struct data d = {0};
  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  d.loop = pw_main_loop_new(NULL);

  pw_loop_add_signal(pw_main_loop_get_loop(d.loop), SIGINT, do_quit, &d);
  pw_loop_add_signal(pw_main_loop_get_loop(d.loop), SIGTERM, do_quit, &d);

  d.filter = pw_filter_new_simple(
      pw_main_loop_get_loop(d.loop), "audio-filter",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                        "Filter", PW_KEY_MEDIA_ROLE, "DSP", NULL),
      &filter_events, &d);

  // Input port
  d.in_port = pw_filter_add_port(
      d.filter, PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
      sizeof(struct port),
      pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                        PW_KEY_PORT_NAME, "input", NULL),
      NULL, 0);

  // Output port
  d.out_port = pw_filter_add_port(
      d.filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
      sizeof(struct port),
      pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                        PW_KEY_PORT_NAME, "output", NULL),
      NULL, 0);

  params[0] = spa_process_latency_build(
      &b, SPA_PARAM_ProcessLatency,
      &SPA_PROCESS_LATENCY_INFO_INIT(.ns = 10 * SPA_NSEC_PER_MSEC));

  if (pw_filter_connect(d.filter, PW_FILTER_FLAG_RT_PROCESS, params, 1) < 0) {
    fprintf(stderr, "can't connect\n");
    goto fin;
  }

  pw_main_loop_run(d.loop);

  pw_filter_destroy(d.filter);
  pw_main_loop_destroy(d.loop);

fin:
  pthread_exit(NULL);
}

void *thread_proc_vinput(void *user_data) {
  (void)user_data;
  EventListener listener;
  VInputError err = EventListener2_create(&listener, true, false, false);
  if (err != VINPUT_OK) {
    printf("Failed to init event listener: %s.", VInput_error_get_message(err));
    exit(1);
  }

  err = EventListener2_start(&listener, on_key, NULL, NULL);
  if (err != VINPUT_OK) {
    printf("Failed to start event listener: %s.",
           VInput_error_get_message(err));
    exit(1);
  }

  EventListener_free(&listener);

  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
  pw_init(&argc, &argv);

  pthread_t pw_thread, vinput_thread;
  int res = pthread_create(&pw_thread, NULL, thread_proc_pw, NULL);
  if (res) {
    puts("Failed to create pipewire thread!");
    exit(1);
  }

  res = pthread_create(&vinput_thread, NULL, thread_proc_vinput, NULL);
  if (res) {
    puts("Failed to create libvinput thread!");
    exit(1);
  }

  pthread_join(pw_thread, NULL);
  pthread_join(vinput_thread, NULL);

  pw_deinit();
  return 0;
}
