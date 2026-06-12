// Interactive live demo for the vocalexp element.
//
// Builds  autoaudiosrc → vocalexp → autoaudiosink  and maps keystrokes to
// property changes, since gst-launch-1.0 cannot change properties on a
// running pipeline:
//
//   ↑/+ : expressivity +0.1        ↓/- : expressivity −0.1
//   0..4: expressivity = digit     e   : toggle envelope-preservation
//   q   : quit
//
// Usage: vocalexp-demo [--expressivity=1.0] [--frame-size=1024]

#include <gst/gst.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct TerminalRawMode {
  termios saved{};
  bool active = false;

  TerminalRawMode() {
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return;
    termios raw = saved;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    active = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
  }
  ~TerminalRawMode() {
    if (active) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  }
};

void printStatus(GstElement* filter) {
  gfloat expressivity = 0.0f;
  gboolean envelope = FALSE;
  g_object_get(filter, "expressivity", &expressivity, "envelope-preservation", &envelope,
               nullptr);
  printf("\r  expressivity = %.2f   envelope-preservation = %s   (q to quit)   ",
         expressivity, envelope ? "on " : "off");
  fflush(stdout);
}

gboolean onBusMessage(GstBus*, GstMessage* message, gpointer mainLoop) {
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      g_printerr("\nERROR from %s: %s\n%s\n", GST_OBJECT_NAME(message->src),
                 error->message, debug ? debug : "");
      g_clear_error(&error);
      g_free(debug);
      g_main_loop_quit(static_cast<GMainLoop*>(mainLoop));
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit(static_cast<GMainLoop*>(mainLoop));
      break;
    default:
      break;
  }
  return TRUE;
}

struct KeyContext {
  GstElement* filter;
  GMainLoop* mainLoop;
};

gboolean onKeyboardInput(GIOChannel* channel, GIOCondition, gpointer userData) {
  auto* context = static_cast<KeyContext*>(userData);
  gchar key = 0;
  gsize read = 0;
  if (g_io_channel_read_chars(channel, &key, 1, &read, nullptr) != G_IO_STATUS_NORMAL ||
      read == 0) {
    return TRUE;
  }

  gfloat expressivity = 0.0f;
  gboolean envelope = FALSE;
  g_object_get(context->filter, "expressivity", &expressivity, "envelope-preservation",
               &envelope, nullptr);

  switch (key) {
    case 'q':
      g_main_loop_quit(context->mainLoop);
      return TRUE;
    case 'e':
      g_object_set(context->filter, "envelope-preservation", !envelope, nullptr);
      break;
    case '+':
    case 'A':  // up-arrow tail byte of the ESC [ A sequence
      g_object_set(context->filter, "expressivity",
                   std::min(expressivity + 0.1f, 4.0f), nullptr);
      break;
    case '-':
    case 'B':  // down-arrow tail byte
      g_object_set(context->filter, "expressivity",
                   std::max(expressivity - 0.1f, 0.0f), nullptr);
      break;
    default:
      if (key >= '0' && key <= '4') {
        g_object_set(context->filter, "expressivity", static_cast<float>(key - '0'),
                     nullptr);
      }
      break;
  }
  printStatus(context->filter);
  return TRUE;
}

}  // namespace

int main(int argc, char** argv) {
  gdouble expressivity = 1.0;
  gint frameSize = 1024;
  GOptionEntry entries[] = {
      {"expressivity", 'x', 0, G_OPTION_ARG_DOUBLE, &expressivity,
       "Initial expressivity (0..4)", "E"},
      {"frame-size", 'f', 0, G_OPTION_ARG_INT, &frameSize,
       "STFT frame size in samples (power of two)", "N"},
      {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
  };

  GOptionContext* options = g_option_context_new("- live vocalexp demo");
  g_option_context_add_main_entries(options, entries, nullptr);
  g_option_context_add_group(options, gst_init_get_option_group());
  GError* error = nullptr;
  if (!g_option_context_parse(options, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    return 1;
  }
  g_option_context_free(options);

  std::string description =
      "autoaudiosrc ! queue max-size-time=20000000 leaky=downstream ! "
      "audioconvert ! audioresample ! "
      "audio/x-raw,format=F32LE,channels=1,rate=48000 ! "
      "vocalexp name=filter frame-size=" + std::to_string(frameSize) +
      " expressivity=" + std::to_string(expressivity) + " ! "
      "audioconvert ! autoaudiosink";

  GstElement* pipeline = gst_parse_launch(description.c_str(), &error);
  if (!pipeline) {
    g_printerr("Failed to build pipeline: %s\n", error ? error->message : "unknown");
    g_clear_error(&error);
    return 1;
  }

  GstElement* filter = gst_bin_get_by_name(GST_BIN(pipeline), "filter");
  GMainLoop* mainLoop = g_main_loop_new(nullptr, FALSE);

  GstBus* bus = gst_element_get_bus(pipeline);
  gst_bus_add_watch(bus, onBusMessage, mainLoop);

  TerminalRawMode rawMode;
  KeyContext keyContext{filter, mainLoop};
  GIOChannel* stdinChannel = g_io_channel_unix_new(STDIN_FILENO);
  g_io_add_watch(stdinChannel, G_IO_IN, onKeyboardInput, &keyContext);

  printf("vocalexp live demo — speak into the microphone\n");
  printf("  +/- or arrows: expressivity ±0.1 | 0-4: set directly | e: toggle envelope | q: quit\n");
  printStatus(filter);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_main_loop_run(mainLoop);
  printf("\n");

  gst_element_set_state(pipeline, GST_STATE_NULL);
  g_io_channel_unref(stdinChannel);
  gst_object_unref(bus);
  gst_object_unref(filter);
  gst_object_unref(pipeline);
  g_main_loop_unref(mainLoop);
  return 0;
}
