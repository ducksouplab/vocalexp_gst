/* vocalexp: real-time vocal expressivity transformation.
 *
 * GstAudioFilter wrapping vocalexp::VocalExpressivityProcessor — YIN pitch
 * tracking → expressivity contour scaling → phase-vocoder pitch shifting
 * with optional spectral-envelope (formant) preservation.
 *
 * Example:
 *   gst-launch-1.0 autoaudiosrc ! audioconvert ! audioresample !
 *     audio/x-raw,format=F32LE,channels=1,rate=48000 !
 *     vocalexp expressivity=1.8 envelope-preservation=true !
 *     audioconvert ! autoaudiosink
 */

#include "gst/gstvocalexp.hpp"

#include <memory>
#include <mutex>
#include <new>

#include "dsp/vocal_expressivity_processor.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_vocalexp_debug);
#define GST_CAT_DEFAULT gst_vocalexp_debug

namespace {

constexpr float kDefaultExpressivity = 1.0f;
constexpr gboolean kDefaultEnvelopePreservation = TRUE;
constexpr guint kDefaultFrameSize = 1024;
constexpr guint kDefaultOverlapFactor = 4;
constexpr float kDefaultMinFrequency = 60.0f;
constexpr float kDefaultMaxFrequency = 1000.0f;
constexpr vocalexp::VocalExpressivityProcessor::Engine kDefaultEngine =
    vocalexp::VocalExpressivityProcessor::Engine::LEGACY;

enum {
  PROP_0,
  PROP_EXPRESSIVITY,
  PROP_ENVELOPE_PRESERVATION,
  PROP_ENGINE,
  PROP_FRAME_SIZE,
  PROP_OVERLAP_FACTOR,
  PROP_MIN_FREQUENCY,
  PROP_MAX_FREQUENCY,
  PROP_VERBOSE,
};

// Mono float: one pitch contour per stream is the model; pipelines mix down
// with audioconvert. All rates work — frame/hop sizes are in samples.
#define VOCALEXP_CAPS                              \
  "audio/x-raw, "                                  \
  "format = (string) " GST_AUDIO_NE(F32) ", "      \
  "rate = (int) [ 8000, 192000 ], "                \
  "channels = (int) 1, "                           \
  "layout = (string) interleaved"

}  // namespace

struct _GstVocalexp {
  GstAudioFilter parent;

  /* Guards processor_ and the cached parameters below. The streaming thread
   * holds it for the duration of each transform; property setters hold it
   * only to flip parameters, so contention is a few instructions. */
  std::mutex* lock;
  std::unique_ptr<vocalexp::VocalExpressivityProcessor>* processor;

  gfloat expressivity;
  gboolean envelopePreservation;
  vocalexp::VocalExpressivityProcessor::Engine engine;
  guint frameSize;
  guint overlapFactor;
  gfloat minFrequency;
  gfloat maxFrequency;
  gboolean verbose;
};

G_DEFINE_TYPE_WITH_CODE(GstVocalexp, gst_vocalexp, GST_TYPE_AUDIO_FILTER,
                        GST_DEBUG_CATEGORY_INIT(gst_vocalexp_debug, "vocalexp", 0,
                                                "vocal expressivity filter"));

#define GST_TYPE_VOCALEXP_ENGINE (gst_vocalexp_engine_get_type())
static GType
gst_vocalexp_engine_get_type (void)
{
  static GType engine_type = 0;
  static const GEnumValue engine_values[] = {
    {static_cast<gint>(vocalexp::VocalExpressivityProcessor::Engine::LEGACY), "LEGACY (YIN + Phase Vocoder)", "legacy"},
    {static_cast<gint>(vocalexp::VocalExpressivityProcessor::Engine::MODERN), "MODERN (SWIFT-F0 + RubberBand)", "modern"},
    {0, NULL, NULL}
  };

  if (!engine_type) {
    engine_type = g_enum_register_static("GstVocalexpEngine", engine_values);
  }
  return engine_type;
}

static void gst_vocalexp_set_property(GObject* object, guint propertyId,
                                      const GValue* value, GParamSpec* pspec) {
  GstVocalexp* self = GST_VOCALEXP(object);
  std::lock_guard<std::mutex> guard(*self->lock);

  switch (propertyId) {
    case PROP_EXPRESSIVITY:
      self->expressivity = g_value_get_float(value);
      if (*self->processor) (*self->processor)->setExpressivity(self->expressivity);
      break;
    case PROP_ENVELOPE_PRESERVATION:
      self->envelopePreservation = g_value_get_boolean(value);
      if (*self->processor) {
        (*self->processor)->setEnvelopePreservation(self->envelopePreservation != FALSE);
      }
      break;
    case PROP_ENGINE:
      self->engine = static_cast<vocalexp::VocalExpressivityProcessor::Engine>(g_value_get_enum(value));
      break;
    case PROP_FRAME_SIZE:
      self->frameSize = g_value_get_uint(value);
      break;
    case PROP_OVERLAP_FACTOR:
      self->overlapFactor = g_value_get_uint(value);
      break;
    case PROP_MIN_FREQUENCY:
      self->minFrequency = g_value_get_float(value);
      break;
    case PROP_MAX_FREQUENCY:
      self->maxFrequency = g_value_get_float(value);
      break;
    case PROP_VERBOSE:
      self->verbose = g_value_get_boolean(value);
      if (*self->processor) (*self->processor)->setVerbose(self->verbose != FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, pspec);
      break;
  }
}

static void gst_vocalexp_get_property(GObject* object, guint propertyId, GValue* value,
                                      GParamSpec* pspec) {
  GstVocalexp* self = GST_VOCALEXP(object);
  std::lock_guard<std::mutex> guard(*self->lock);

  switch (propertyId) {
    case PROP_EXPRESSIVITY:
      g_value_set_float(value, self->expressivity);
      break;
    case PROP_ENVELOPE_PRESERVATION:
      g_value_set_boolean(value, self->envelopePreservation);
      break;
    case PROP_ENGINE:
      g_value_set_enum(value, static_cast<gint>(self->engine));
      break;
    case PROP_FRAME_SIZE:
      g_value_set_uint(value, self->frameSize);
      break;
    case PROP_OVERLAP_FACTOR:
      g_value_set_uint(value, self->overlapFactor);
      break;
    case PROP_MIN_FREQUENCY:
      g_value_set_float(value, self->minFrequency);
      break;
    case PROP_MAX_FREQUENCY:
      g_value_set_float(value, self->maxFrequency);
      break;
    case PROP_VERBOSE:
      g_value_set_boolean(value, self->verbose);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, pspec);
      break;
  }
}

/* Negotiated format changed: rebuild the processor for the new rate. */
static gboolean gst_vocalexp_setup(GstAudioFilter* filter, const GstAudioInfo* info) {
  GstVocalexp* self = GST_VOCALEXP(filter);
  std::lock_guard<std::mutex> guard(*self->lock);

  if (self->frameSize % self->overlapFactor != 0) {
    GST_ELEMENT_ERROR(self, LIBRARY, SETTINGS, (nullptr),
                      ("frame-size %u is not divisible by overlap-factor %u",
                       self->frameSize, self->overlapFactor));
    return FALSE;
  }

  vocalexp::VocalExpressivityProcessor::Config config;
  config.sampleRate = static_cast<float>(GST_AUDIO_INFO_RATE(info));
  config.engine = self->engine;
  config.expressivity = self->expressivity;
  config.envelopePreservation = self->envelopePreservation != FALSE;
  config.frameSize = self->frameSize;
  config.overlapFactor = self->overlapFactor;
  config.minFrequency = self->minFrequency;
  config.maxFrequency = self->maxFrequency;
  config.verbose = self->verbose != FALSE;

  try {
    self->processor->reset(new vocalexp::VocalExpressivityProcessor(config));
  } catch (const std::exception& e) {
    GST_ELEMENT_ERROR(self, LIBRARY, SETTINGS, (nullptr), ("%s", e.what()));
    return FALSE;
  }

  GST_INFO_OBJECT(self,
                  "configured: rate=%d frame-size=%u overlap=%u latency=%" G_GSIZE_FORMAT
                  " samples",
                  GST_AUDIO_INFO_RATE(info), self->frameSize, self->overlapFactor,
                  (*self->processor)->latencySamples());

  /* The STFT latency depends on the negotiated rate; tell the pipeline. */
  gst_element_post_message(GST_ELEMENT(self),
                           gst_message_new_latency(GST_OBJECT(self)));
  return TRUE;
}

static GstFlowReturn gst_vocalexp_transform_ip(GstBaseTransform* base, GstBuffer* buffer) {
  GstVocalexp* self = GST_VOCALEXP(base);

  if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT)) {
    std::lock_guard<std::mutex> guard(*self->lock);
    if (*self->processor) (*self->processor)->reset();
  }

  GstMapInfo map;
  if (!gst_buffer_map(buffer, &map, GST_MAP_READWRITE)) {
    GST_ELEMENT_ERROR(self, RESOURCE, READ, (nullptr), ("failed to map buffer"));
    return GST_FLOW_ERROR;
  }

  {
    std::lock_guard<std::mutex> guard(*self->lock);
    if (*self->processor) {
      float* samples = reinterpret_cast<float*>(map.data);
      const std::size_t n = map.size / sizeof(float);
      (*self->processor)->process(samples, samples, n);
    }
  }

  gst_buffer_unmap(buffer, &map);
  return GST_FLOW_OK;
}

static gboolean gst_vocalexp_query(GstBaseTransform* base, GstPadDirection direction,
                                   GstQuery* query) {
  GstVocalexp* self = GST_VOCALEXP(base);

  if (direction == GST_PAD_SRC && GST_QUERY_TYPE(query) == GST_QUERY_LATENCY) {
    /* Let upstream answer first, then add our STFT latency. */
    GstPad* sinkPad = GST_BASE_TRANSFORM_SINK_PAD(base);
    if (!gst_pad_peer_query(sinkPad, query)) return FALSE;

    GstClockTime ours = 0;
    {
      std::lock_guard<std::mutex> guard(*self->lock);
      const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(self);
      if (*self->processor && GST_AUDIO_INFO_RATE(info) > 0) {
        ours = gst_util_uint64_scale_int((*self->processor)->latencySamples(), GST_SECOND,
                                         GST_AUDIO_INFO_RATE(info));
      }
    }

    gboolean live = FALSE;
    GstClockTime min = 0, max = GST_CLOCK_TIME_NONE;
    gst_query_parse_latency(query, &live, &min, &max);
    min += ours;
    if (GST_CLOCK_TIME_IS_VALID(max)) max += ours;
    gst_query_set_latency(query, live, min, max);
    GST_DEBUG_OBJECT(self, "reported latency: +%" GST_TIME_FORMAT, GST_TIME_ARGS(ours));
    return TRUE;
  }

  GstBaseTransformClass* parent = GST_BASE_TRANSFORM_CLASS(gst_vocalexp_parent_class);
  return parent->query ? parent->query(base, direction, query) : FALSE;
}

static gboolean gst_vocalexp_stop(GstBaseTransform* base) {
  GstVocalexp* self = GST_VOCALEXP(base);
  {
    std::lock_guard<std::mutex> guard(*self->lock);
    self->processor->reset();
  }
  GstBaseTransformClass* parent = GST_BASE_TRANSFORM_CLASS(gst_vocalexp_parent_class);
  return parent->stop ? parent->stop(base) : TRUE;
}

static void gst_vocalexp_finalize(GObject* object) {
  GstVocalexp* self = GST_VOCALEXP(object);
  delete self->processor;
  delete self->lock;
  G_OBJECT_CLASS(gst_vocalexp_parent_class)->finalize(object);
}

static void gst_vocalexp_class_init(GstVocalexpClass* klass) {
  GObjectClass* gobjectClass = G_OBJECT_CLASS(klass);
  GstElementClass* elementClass = GST_ELEMENT_CLASS(klass);
  GstBaseTransformClass* baseTransformClass = GST_BASE_TRANSFORM_CLASS(klass);
  GstAudioFilterClass* audioFilterClass = GST_AUDIO_FILTER_CLASS(klass);

  gobjectClass->set_property = gst_vocalexp_set_property;
  gobjectClass->get_property = gst_vocalexp_get_property;
  gobjectClass->finalize = gst_vocalexp_finalize;

  g_object_class_install_property(
      gobjectClass, PROP_EXPRESSIVITY,
      g_param_spec_float(
          "expressivity", "Expressivity",
          "Pitch contour scaling factor E: 1.0 = unchanged, 0.0 = monotone, "
          "> 1.0 = exaggerated intonation",
          0.0f, 4.0f, kDefaultExpressivity,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                   GST_PARAM_CONTROLLABLE)));

  g_object_class_install_property(
      gobjectClass, PROP_ENVELOPE_PRESERVATION,
      g_param_spec_boolean(
          "envelope-preservation", "Envelope preservation",
          "Preserve the spectral envelope (formants / vocal identity) while "
          "shifting pitch",
          kDefaultEnvelopePreservation,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                   GST_PARAM_CONTROLLABLE)));

  g_object_class_install_property(
      gobjectClass, PROP_ENGINE,
      g_param_spec_enum("engine", "Engine",
                        "Processing engine: LEGACY (YIN + Phase Vocoder) or "
                        "MODERN (SWIFT-F0 + RubberBand)",
                        GST_TYPE_VOCALEXP_ENGINE, static_cast<gint>(kDefaultEngine),
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobjectClass, PROP_FRAME_SIZE,
      g_param_spec_uint(
          "frame-size", "STFT frame size",
          "STFT window size in samples (power of two). Latency is "
          "frame-size - frame-size/overlap-factor samples. Takes effect on "
          "the next caps negotiation.",
          128, 8192, kDefaultFrameSize,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobjectClass, PROP_OVERLAP_FACTOR,
      g_param_spec_uint(
          "overlap-factor", "STFT overlap factor",
          "Number of overlapping analysis windows (hop = frame-size / "
          "overlap-factor). Takes effect on the next caps negotiation.",
          2, 16, kDefaultOverlapFactor,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobjectClass, PROP_MIN_FREQUENCY,
      g_param_spec_float(
          "min-frequency", "Minimum pitch",
          "Lower bound of the pitch search range in Hz. Takes effect on the "
          "next caps negotiation.",
          20.0f, 2000.0f, kDefaultMinFrequency,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobjectClass, PROP_MAX_FREQUENCY,
      g_param_spec_float(
          "max-frequency", "Maximum pitch",
          "Upper bound of the pitch search range in Hz. Takes effect on the "
          "next caps negotiation.",
          20.0f, 4000.0f, kDefaultMaxFrequency,
          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

          g_object_class_install_property(
          gobjectClass, PROP_VERBOSE,
          g_param_spec_boolean(
          "verbose", "Verbose logging",
          "Log internal DSP state (F0, ratio) to 'vocalexp_debug.csv'",
          FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

          gst_element_class_set_static_metadata(
      elementClass, "Vocal expressivity transformer", "Filter/Effect/Audio",
      "Scales the expressivity of a voice's pitch contour in real time via "
      "YIN pitch tracking and a formant-preserving phase vocoder",
      "ducksouplab <https://github.com/ducksouplab>");

  GstCaps* caps = gst_caps_from_string(VOCALEXP_CAPS);
  gst_audio_filter_class_add_pad_templates(audioFilterClass, caps);
  gst_caps_unref(caps);

  audioFilterClass->setup = GST_DEBUG_FUNCPTR(gst_vocalexp_setup);
  baseTransformClass->transform_ip = GST_DEBUG_FUNCPTR(gst_vocalexp_transform_ip);
  baseTransformClass->query = GST_DEBUG_FUNCPTR(gst_vocalexp_query);
  baseTransformClass->stop = GST_DEBUG_FUNCPTR(gst_vocalexp_stop);
  baseTransformClass->transform_ip_on_passthrough = FALSE;
}

static void gst_vocalexp_init(GstVocalexp* self) {
  self->lock = new std::mutex();
  self->processor = new std::unique_ptr<vocalexp::VocalExpressivityProcessor>();
  self->expressivity = kDefaultExpressivity;
  self->envelopePreservation = kDefaultEnvelopePreservation;
  self->engine = kDefaultEngine;
  self->frameSize = kDefaultFrameSize;
  self->overlapFactor = kDefaultOverlapFactor;
  self->minFrequency = kDefaultMinFrequency;
  self->maxFrequency = kDefaultMaxFrequency;
  self->verbose = FALSE;
}

gboolean gst_vocalexp_element_register(GstPlugin* plugin) {
  return gst_element_register(plugin, "vocalexp", GST_RANK_NONE, GST_TYPE_VOCALEXP);
}

#ifndef PACKAGE
#define PACKAGE "vocalexp"
#endif
#ifndef VERSION
#define VERSION "0.1.0"
#endif

static gboolean plugin_init(GstPlugin* plugin) {
  return gst_vocalexp_element_register(plugin);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, vocalexp,
                  "Real-time vocal expressivity transformation", plugin_init, VERSION,
                  "MIT/X11", PACKAGE, "https://github.com/ducksouplab/vocalexp_gst")
