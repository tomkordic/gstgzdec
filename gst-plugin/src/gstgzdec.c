/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2024  <<user@hostname.org>>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-gzdec
 *
 * FIXME:Describe gzdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! gzdec ! fakesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// #include "gstbzdec.h"
#include "gstgzdec.h"
#include <gst/base/gsttypefindhelper.h>
#include <gst/gstplugin.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_gzdec_debug);
#define GST_CAT_DEFAULT gst_gzdec_debug

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum { PROP_0 };

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("ANY"));

#define gst_gzdec_parent_class parent_class
G_DEFINE_TYPE(Gstgzdec, gst_gzdec, GST_TYPE_ELEMENT);

GST_ELEMENT_REGISTER_DEFINE(gzdec, "gzdec", GST_RANK_NONE, GST_TYPE_GZDEC);

static void gst_gzdec_set_property(GObject *object, guint prop_id,
                                   const GValue *value, GParamSpec *pspec);
static void gst_gzdec_get_property(GObject *object, guint prop_id,
                                   GValue *value, GParamSpec *pspec);

static gboolean gst_gzdec_sink_event(GstPad *pad, GstObject *parent,
                                     GstEvent *event);
static GstFlowReturn gst_gzdec_chain(GstPad *pad, GstObject *parent,
                                     GstBuffer *buf);

/* bzlib functions */

void gstgzdec_bz_decompress_end(Gstgzdec *dec) {
  g_return_if_fail(GST_IS_GZDEC(dec));

  if (dec->bzlib_ready) {
    GST_DEBUG_OBJECT(dec, "Releaseing bzlib");
    BZ2_bzDecompressEnd(&dec->bz_stream);
    memset(&dec->bz_stream, 0, sizeof(dec->bz_stream));
    dec->bzlib_ready = FALSE;
  }
}

void gstgzdec_bz_decompress_init(Gstgzdec *dec) {
  g_return_if_fail(GST_IS_GZDEC(dec));

  gstgzdec_bz_decompress_end(dec);
  dec->offset = 0;
  switch (BZ2_bzDecompressInit(&dec->bz_stream, 0, 0)) {
  case BZ_OK:
    dec->bzlib_ready = TRUE;
    return;
  default:
    dec->bzlib_ready = FALSE;
    GST_ELEMENT_ERROR(dec, CORE, FAILED, (NULL),
                      ("Failed to start bzlib decompression."));
    return;
  }
}

/* zlib functions */

static void gstgzdec_zlib_decompress_end(Gstgzdec *dec) {
  if (dec->zlib_ready) {
    GST_DEBUG_OBJECT(dec, "Releaseing zlib");
    int ret = inflateEnd(&dec->stream);
    if (ret != Z_OK) {
      GST_ERROR_OBJECT(dec, "Failed to release zlib");
    } else {
      GST_DEBUG_OBJECT(dec, "Zlib released");
    }
    memset(&dec->stream, 0, sizeof(dec->stream));
    dec->zlib_ready = FALSE;
  }
}

static void gstgzdec_zlib_decompress_init(Gstgzdec *dec) {
  GST_DEBUG_OBJECT(dec, "Initializing zlib");
  dec->offset = 0;
  // Release zlib if initialized
  gstgzdec_zlib_decompress_end(dec);
  dec->stream.zalloc = Z_NULL;
  dec->stream.zfree = Z_NULL;
  dec->stream.opaque = Z_NULL;
  dec->stream.avail_in = 0;
  dec->stream.next_in = Z_NULL;
  int ret = inflateInit2(&dec->stream, MAX_WBITS | 32);
  if (ret != Z_OK) {
    GST_DEBUG_OBJECT(dec, "Failed to initialize zlib for decompression");
    dec->zlib_ready = FALSE;
  } else {
    GST_DEBUG_OBJECT(dec, "Zlib ready");
    dec->zlib_ready = TRUE;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean gst_gzdec_sink_event(GstPad *pad, GstObject *parent,
                                     GstEvent *event) {
  Gstgzdec *filter;
  gboolean ret;

  filter = GST_GZDEC(parent);

  GST_LOG_OBJECT(filter, "Received %s event: %" GST_PTR_FORMAT,
                 GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_CAPS: {
    GstCaps *caps;

    gst_event_parse_caps(event, &caps);
    /* do something with the caps */

    /* and forward */
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  default:
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  return ret;
}

int gstgzdec_zlib_deflate(Gstgzdec *dec) {
  int ret = inflate(&dec->stream, Z_NO_FLUSH);
  switch (ret) {
  case Z_OK:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_OK [%s]", dec->stream.msg);
    break;
  case Z_STREAM_END:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_STREAM_END [%s]",
                     dec->stream.msg);
    break;
  case Z_NEED_DICT:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_NEED_DICT [%s]", dec->stream.msg);
    break;
    /* Errors */
  case Z_ERRNO:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_ERRNO [%s]", dec->stream.msg);
    break;
  case Z_STREAM_ERROR:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_STREAM_ERROR [%s]",
                     dec->stream.msg);
    break;
  case Z_DATA_ERROR:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_DATA_ERROR [%s]",
                     dec->stream.msg);
    break;
  case Z_MEM_ERROR:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_MEM_ERROR [%s]", dec->stream.msg);
    break;
  case Z_BUF_ERROR:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_BUF_ERROR [%s]", dec->stream.msg);
    break;
  case Z_VERSION_ERROR:
    GST_DEBUG_OBJECT(dec, "inflate() return Z_VERSION_ERROR [%s]",
                     dec->stream.msg);
  default:
    GST_DEBUG_OBJECT(dec, "inflate() return unknow code (%d) [%s]", ret,
                     dec->stream.msg);
    break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn gst_gzdec_chain(GstPad *pad, GstObject *parent,
                                     GstBuffer *in) {
  GstFlowReturn flow = GST_FLOW_OK;
  GstBuffer *out;
  Gstgzdec *dec;
  int ret = Z_OK;
  GstMapInfo inmap = GST_MAP_INFO_INIT, outmap;

  dec = GST_GZDEC(parent);
  if (!dec->zlib_ready || !dec->bzlib_ready) {
    /* Don't go further if not ready */
    GST_ELEMENT_ERROR(dec, LIBRARY, FAILED, (NULL),
                      ("Decompressor not ready."));
    flow = GST_FLOW_FLUSHING;
  } else {
    /* Mapping the input buffer */
    gst_buffer_map(in, &inmap, GST_MAP_READ);
    dec->stream.next_in = (z_const Bytef *)inmap.data;
    dec->stream.avail_in = inmap.size;

    dec->bz_stream.next_in = (z_const Bytef *)inmap.data;
    dec->bz_stream.avail_in = inmap.size;

    GST_DEBUG_OBJECT(dec, "Input buffer size : dec->stream.avail_in = %d",
                     dec->stream.avail_in);
    do {
      guint have;
      /* Create and map the output buffer */
      out = gst_buffer_new_and_alloc(CHUNK_SIZE);
      gst_buffer_map(out, &outmap, GST_MAP_WRITE);
      dec->stream.next_out = (Bytef *)outmap.data;
      dec->stream.avail_out = outmap.size;

      /* Try to decode */
      gst_buffer_unmap(out, &outmap);
      if (!dec->bz_buffer_detected) {
        ret = gstgzdec_zlib_deflate(dec);
      } else {
        // procede with bz deflate
        ret = Z_DATA_ERROR;
      }
      if (ret == Z_DATA_ERROR) {
        GST_DEBUG_OBJECT(dec, "Trying to deflate bz2 buffer");
        dec->bz_stream.next_out = (Bytef *)outmap.data;
        dec->bz_stream.avail_out = outmap.size;
        ret = BZ2_bzDecompress(&dec->bz_stream);
        if ((ret != BZ_OK) && (ret != BZ_STREAM_END) &&
            (ret != BZ_SEQUENCE_ERROR)) {
          GST_ELEMENT_ERROR(
              dec, STREAM, DECODE, (NULL),
              ("Failed to decompress bz data (error code %i).", ret));
          flow = GST_FLOW_ERROR;
          gstgzdec_bz_decompress_init(dec);
          gst_buffer_unref(out);
          dec->bz_buffer_detected = FALSE;
          break;
        }
        GST_DEBUG_OBJECT(dec, "bz2 buffer deflated, adjusting the buffer data");
        dec->bz_buffer_detected = TRUE;
        if (dec->bz_stream.avail_out >= gst_buffer_get_size(out)) {
          gst_buffer_unref(out);
          break;
        }
        gst_buffer_resize(out, 0,
                          gst_buffer_get_size(out) - dec->bz_stream.avail_out);
        GST_BUFFER_OFFSET(out) =
            dec->bz_stream.total_out_lo32 - gst_buffer_get_size(out);
      } else {
        dec->bz_buffer_detected = FALSE;
        if (ret == Z_STREAM_ERROR) {
          GST_ELEMENT_ERROR(
              dec, STREAM, DECODE, (NULL),
              ("Failed to decompress data (error code %i).", ret));
          gstgzdec_zlib_decompress_init(dec);
          gst_buffer_unref(out);
          flow = GST_FLOW_ERROR;
          break;
        }
        if (dec->stream.avail_out >= gst_buffer_get_size(out)) {
          gst_buffer_unref(out);
          break;
        }
        /* Resize the output buffer */
        gst_buffer_resize(out, 0,
                          gst_buffer_get_size(out) - dec->stream.avail_out);
        GST_BUFFER_OFFSET(out) =
            dec->stream.total_out - gst_buffer_get_size(out);
      }

      /* Configure source pad (if necessary) */
      if (!dec->offset) {
        GstCaps *caps = NULL;

        caps = gst_type_find_helper_for_buffer(GST_OBJECT(dec), out, NULL);
        if (caps) {
          gst_pad_set_caps(dec->srcpad, caps);
          gst_pad_use_fixed_caps(dec->srcpad);
          gst_caps_unref(caps);
        } else {
          GST_FIXME_OBJECT(
              dec, "shouldn't we queue output buffers until we have a type?");
        }
      }

      /* Push data */
      GST_DEBUG_OBJECT(dec, "Push data on src pad");
      have = gst_buffer_get_size(out);
      flow = gst_pad_push(dec->srcpad, out);
      if (flow != GST_FLOW_OK) {
        GST_DEBUG_OBJECT(dec, "Breaking flow with %i", flow);
        break;
      }
      dec->offset += have;
    } while (ret != Z_STREAM_END);
  }
  gst_buffer_unmap(in, &inmap);
  gst_buffer_unref(in);
  return flow;
}

gst_gzdec_finalize(GObject *object) {
  Gstgzdec *dec = GST_GZDEC(object);
  GST_DEBUG_OBJECT(dec, "Finalize gzdec");
  gstgzdec_zlib_decompress_end(dec);
  gstgzdec_bz_decompress_end(dec);
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* GObject vmethod implementations */

/* initialize the gzdec's class */
static void gst_gzdec_class_init(GstgzdecClass *klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;

  gobject_class->finalize = gst_gzdec_finalize;

  gst_element_class_set_details_simple(
      gstelement_class, "gzdec", "FIXME:Generic",
      "FIXME:Generic Template Element", " <<user@hostname.org>>");

  gst_element_class_add_pad_template(gstelement_class,
                                     gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(
      gstelement_class, gst_static_pad_template_get(&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void gst_gzdec_init(Gstgzdec *dec) {
  dec->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(dec->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_gzdec_sink_event));
  gst_pad_set_chain_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_gzdec_chain));
  GST_PAD_SET_PROXY_CAPS(dec->sinkpad);
  gst_element_add_pad(GST_ELEMENT(dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS(dec->srcpad);
  gst_element_add_pad(GST_ELEMENT(dec), dec->srcpad);
  dec->zlib_ready = FALSE;
  dec->bzlib_ready = FALSE;
  dec->bz_buffer_detected = FALSE;
  gstgzdec_zlib_decompress_init(dec);
  gstgzdec_bz_decompress_init(dec);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean gzdec_init(Gstgzdec *gzdec) {
  GST_DEBUG_CATEGORY_INIT(gst_gzdec_debug, "gzdec", 0, "gz decompress element");

  return GST_ELEMENT_REGISTER(gzdec, gzdec);
}

/* gstreamer looks for this structure to register gzdecs
 *
 * exchange the string 'Template gzdec' with your gzdec description
 */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gzdec, "gzdec",
                  gzdec_init, PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)
