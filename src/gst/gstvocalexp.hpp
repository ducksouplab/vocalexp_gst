#pragma once

#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_VOCALEXP (gst_vocalexp_get_type())
G_DECLARE_FINAL_TYPE(GstVocalexp, gst_vocalexp, GST, VOCALEXP, GstAudioFilter)

/// Registers the vocalexp element with GStreamer; shared between the plugin
/// entry point and the unit tests (which register it statically).
gboolean gst_vocalexp_element_register(GstPlugin* plugin);

G_END_DECLS
