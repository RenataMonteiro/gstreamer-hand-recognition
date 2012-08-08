/*
 * GStreamer hand gesture detect plugins
 * Copyright (C) 2012 Andol Li <<andol@andol.info>>
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
 * SECTION:element-handdetect
 *
 * FIXME:operates hand gesture detection in video streams and images,
 * and enable media operation e.g. play/stop/fast forward/back rewind.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch autovideosrc ! ffmpegcolorspace ! "video/x-raw-rgb, width=320, height=240" ! \
   videoscale ! handdetect ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
/* interfaces */
#include <gst/interfaces/navigation.h>
/* element header */
#include "gsthanddetect.h"
/* gst & opencv */
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstopencvutils.h"
/* debugging */
#include <gst/gstinfo.h>
/* #include "debug.h" */

GST_DEBUG_CATEGORY_STATIC (gst_handdetect_debug);
#define GST_CAT_DEFAULT gst_handdetect_debug

/* define haar file dirs */
#define HAAR_FILE "/usr/local/share/opencv/haarcascades/fist.xml"
#define HAAR_FILE_PALM "/usr/local/share/opencv/haarcascades/palm.xml"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_PROFILE,
  PROP_PROFILE_PALM
};

/* the capabilities of the inputs and outputs.
 *
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static void gst_handdetect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_handdetect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_handdetect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);
static GstFlowReturn gst_handdetect_transform (GstOpencvVideoFilter * transform,
    GstBuffer * buffer, IplImage * img, GstBuffer * outbuf, IplImage * outimg);

static void gst_handdetect_load_profile (Gsthanddetect * filter);

static void gst_handdetect_navigation_send_event (GstNavigation * navigation, GstStructure * structure);
static gboolean gst_handdetect_interface_supported (GstImplementsInterface * iface, GType type);
static void gst_handdetect_interface_init (GstImplementsInterfaceClass * klass);
static void gst_handdetect_init_interfaces (GType type);
static gboolean gst_handdetect_handle_pad_event (GstPad * pad, GstEvent * event);

GST_BOILERPLATE_FULL (Gsthanddetect, gst_handdetect, GstOpencvVideoFilter,
    GST_TYPE_OPENCV_VIDEO_FILTER, gst_handdetect_init_interfaces);

static void
gst_handdetect_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  Gsthanddetect *filter = GST_HANDDETECT (navigation);
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (filter);
  GstEvent *event;
  GstPad *pad = trans->srcpad;

  event = gst_event_new_navigation (structure);

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event))
    gst_pad_send_event (pad, event);
}

static gboolean
gst_handdetect_interface_supported (GstImplementsInterface * iface, GType type)
{
  if (type == GST_TYPE_NAVIGATION)
    return TRUE;
  return FALSE;
}

static void
gst_handdetect_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_handdetect_interface_supported;
}

static void
gst_handdetect_init_interfaces (GType type)
{
  static const GInterfaceInfo iface_info = {
    (GInterfaceInitFunc) gst_handdetect_interface_init,
    NULL,
    NULL,
  };
  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &iface_info);
}

/* handle element pad event */
static gboolean
gst_handdetect_handle_pad_event (GstPad * pad, GstEvent * event)
{
  const gchar *type;
  const GstStructure *s = gst_event_get_structure (event);
  type = gst_structure_get_string (s, "event");
  g_print ("Eventtype {%s}\n", type);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_NAVIGATION:{
      if (g_str_equal (type, "fist-move")) {
        g_print ("   Fist-move event detected.\n ");
        uint x, y;
        gst_structure_get_uint (s, "x", &x);
        gst_structure_get_uint (s, "y", &y);
        g_print ("      HandPos:[%d, %d]\n", x, y);
        // to do
      } else if (g_str_equal (type, "palm-move")) {
        // to do
      } else if (g_str_equal (type, "mouse-move")) {
        gdouble x, y;
        gst_structure_get_double (s, "pointer_x", &x);
        gst_structure_get_double (s, "pointer_y", &y);
        g_print ("Mouse-move [%f, %f]\n", x, y);
      } else if (g_str_equal (type, "mouse-button-press")) {
    	  g_print ("Mouse botton press\n");
      } else if (g_str_equal (type, "mouse-button-release")) {
    	  g_print ("Mouse button release\n");
      }
      break;
    }
    default:
      break;
  }
  return gst_pad_event_default (pad, event);
}

static void
gst_navigation_class_init (GstNavigationInterface * iface)
{
  /* default virtual functions */
  iface->send_event = gst_handdetect_navigation_send_event;
}

GType
gst_navigation_get_type (void)
{
  static GType navigation_type = 0;
  static const GTypeInfo navigation_info = {
    sizeof (GstNavigationInterface),
    (GBaseInitFunc) gst_navigation_class_init,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    NULL,
  };

  navigation_type =
      g_type_register_static (G_TYPE_INTERFACE, "GstNavigation",
      &navigation_info, 0);

  return navigation_type;
}

/* clean opencv images and parameters */
static void
gst_handdetect_finalise (GObject * obj)
{
  Gsthanddetect *filter = GST_HANDDETECT (obj);

  if (filter->cvImage)
    cvReleaseImage (&filter->cvImage);
  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);
  g_free (filter->profile);
  g_free (filter->profile_palm);
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* GObject vmethod implementations */
static void
gst_handdetect_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "hand detect",
      "Filter/Effect/Video",
      "Detects hand gestures to support natural gesture-based media operations",
      "Andol Li <<andol@andol.info>>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialise the HANDDETECT class */
static void
gst_handdetect_class_init (GsthanddetectClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gstopencvbasefilter_class->cv_trans_func = gst_handdetect_transform;
  gstopencvbasefilter_class->cv_set_caps = gst_handdetect_set_caps;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_handdetect_finalise);
  gobject_class->set_property = gst_handdetect_set_property;
  gobject_class->get_property = gst_handdetect_get_property;

  g_object_class_install_property (gobject_class,
      PROP_DISPLAY,
      g_param_spec_boolean ("display",
          "Display",
          "Whether the detected hands are highlighted in output frame",
          TRUE, G_PARAM_READWRITE)
      );
  g_object_class_install_property (gobject_class,
      PROP_PROFILE,
      g_param_spec_string ("profile",
          "Profile",
          "Location of Haar cascade file (fist gesture)",
          HAAR_FILE, G_PARAM_READWRITE)
      );
  g_object_class_install_property (gobject_class,
      PROP_PROFILE_PALM,
      g_param_spec_string ("profile_palm",
          "Profile_palm",
          "Location of Haar cascade file (palm gesture)",
          HAAR_FILE_PALM, G_PARAM_READWRITE)
      );
}

/* initialise the new element
 * instantiate pads and add them to element
 * set pad call-back functions
 * initialise instance structure
 */
static void
gst_handdetect_init (Gsthanddetect * filter, GsthanddetectClass * gclass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (filter);

  gst_pad_set_event_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_handdetect_handle_pad_event));

  filter->profile = g_strdup (HAAR_FILE);
  filter->profile_palm = g_strdup (HAAR_FILE_PALM);
  filter->display = TRUE;
  gst_handdetect_load_profile (filter);
}

static void
gst_handdetect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gsthanddetect *filter = GST_HANDDETECT (object);

  switch (prop_id) {
    case PROP_PROFILE_PALM:
      g_free (filter->profile_palm);
      filter->profile_palm = g_value_dup_string (value);
      gst_handdetect_load_profile (filter);
      break;
    case PROP_PROFILE:
      g_free (filter->profile);
      filter->profile = g_value_dup_string (value);
      gst_handdetect_load_profile (filter);
      break;
    case PROP_DISPLAY:
      filter->display = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_handdetect_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  Gsthanddetect *filter = GST_HANDDETECT (object);

  switch (prop_id) {
    case PROP_PROFILE_PALM:
      g_value_set_string (value, filter->profile_palm);
      break;
    case PROP_PROFILE:
      g_value_set_string (value, filter->profile);
      break;
    case PROP_DISPLAY:
      g_value_set_boolean (value, filter->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_handdetect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels)
{
  Gsthanddetect *filter;
  filter = GST_HANDDETECT (transform);

  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);
  filter->cvGray =
      cvCreateImage (cvSize (in_width, in_height), IPL_DEPTH_8U, 1);
  if (filter->cvImage)
    cvReleaseImage (&filter->cvImage);
  filter->cvImage =
      cvCreateImage (cvSize (in_width, in_height), IPL_DEPTH_8U, 3);

  if (!filter->cvStorage)
    filter->cvStorage = cvCreateMemStorage (0);
  else
    cvClearMemStorage (filter->cvStorage);
  if (!filter->cvStorage_palm)
    filter->cvStorage_palm = cvCreateMemStorage (0);
  else
    cvClearMemStorage (filter->cvStorage_palm);

  return TRUE;
}

/* chain function
 * this function does the actual processing 'of hand detect and display'
 */
static GstFlowReturn
//gst_handdetect_chain (GstPad * pad, GstBuffer * buf)
gst_handdetect_transform (GstOpencvVideoFilter * base, GstBuffer * buffer,
    IplImage * img, GstBuffer * outbuf, IplImage * outimg)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (base);

  Gsthanddetect *filter;
  CvSeq *hands;
  CvRect *r;
  GstStructure *s;
  GstMessage *m;
  int i;

  filter = GST_HANDDETECT (base);
  filter->cvImage->imageData = (char *) GST_BUFFER_DATA (buffer);
  /* 320 x 240 is with the best detect accuracy, if not, give warnings */
  if(filter->cvImage->width > 320 || filter->cvImage->height > 240)
	  GST_INFO ("WARNING: resize to 320 x 240 to have best detect accuracy.\n");
  /* cvt to gray colour space for hand detect */
  cvCvtColor (filter->cvImage, filter->cvGray, CV_RGB2GRAY);
  cvClearMemStorage (filter->cvStorage);

  /* ------detect palm gesture and send events------ */
  /* TO DO */

  /* ------detect fist gesture and send events------ */
  if (filter->cvCascade) {
    /* detect hands */
    hands =
        cvHaarDetectObjects (filter->cvImage, filter->cvCascade,
        filter->cvStorage, 1.1, 2, CV_HAAR_DO_CANNY_PRUNING, cvSize (24, 24)
#if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
        , cvSize (0, 0)
#endif
        );

    /* if hands detected, set the buffer writable */
    if (filter->display && hands && hands->total > 0) {
      buffer = gst_buffer_make_writable (buffer);
      /* debug print */
      g_print ("---%d hands detected\n", (int) hands->total);
    }

    /* go through all hands detected to get the best hand
     * prev_r => previous hand
     * best_r => best hand in this frame
     */
    if (hands && hands->total > 0) {
      /* init distance for comparisons, 400 is the maximum distance in 320x240 frame */
      int min_distance = 400;
      /* init filter->prev_r */
      CvRect temp_r = cvRect (0, 0, 0, 0);
      if (filter->prev_r == NULL)
        filter->prev_r = &temp_r;

      /* get the best hand */
      for (i = 0; i < (hands ? hands->total : 0); i++) {
        r = (CvRect *) cvGetSeqElem (hands, i);
        int distance = (int) sqrt (pow ((r->x - filter->prev_r->x),
                2) + pow ((r->y - filter->prev_r->y), 2));
        if (distance <= min_distance) {
          min_distance = distance;
          filter->best_r = r;
        }
      }

      /* save best_r as prev_r for next frame */
      filter->prev_r = (CvRect *) filter->best_r;

      /* define structure for message post */
      s = gst_structure_new ("detected_hand_info",
          "gesture", G_TYPE_STRING, "fist",
          "x", G_TYPE_UINT,
          (uint) (filter->best_r->x + filter->best_r->width * 0.5), "y",
          G_TYPE_UINT,
          (uint) (filter->best_r->y + filter->best_r->height * 0.5), "width",
          G_TYPE_UINT, (uint) filter->best_r->width, "height", G_TYPE_UINT,
          (uint) filter->best_r->height, NULL);
      /* init message element */
      m = gst_message_new_element (GST_OBJECT (filter), s);
      /* send message */
      gst_element_post_message (GST_ELEMENT (filter), m);

      /* define structure for event */
      s = gst_structure_new ("fist-move",
          "event", G_TYPE_STRING, "fist-move",
          "x", G_TYPE_UINT,
          (uint) (filter->best_r->x + filter->best_r->width * 0.5), "y",
          G_TYPE_UINT,
          (uint) (filter->best_r->y + filter->best_r->height * 0.5), "width",
          G_TYPE_UINT, (uint) filter->best_r->width, "height", G_TYPE_UINT,
          (uint) filter->best_r->height, NULL);
      /* set and send navigation event */
      GstEvent *event = gst_event_new_navigation (s);
      gst_pad_send_event (trans->srcpad, event);

      /* check filter->display, if TRUE, display the circle marker in the frame */
      if (filter->display) {
        CvPoint center;
        int radius;
        center.x = cvRound ((filter->best_r->x + filter->best_r->width * 0.5));
        center.y = cvRound ((filter->best_r->y + filter->best_r->height * 0.5));
        radius =
            cvRound ((filter->best_r->width + filter->best_r->height) * 0.25);
        cvCircle (filter->cvImage, center, radius, CV_RGB (0, 0, 200), 1, 8, 0);
      }
    }
  }

  /* just push out the incoming buffer */
  return gst_pad_push (trans->srcpad, buffer);
}

static void
gst_handdetect_load_profile (Gsthanddetect * filter)
{
  GST_INFO ( "Loading profiles...\n");

  filter->cvCascade =
      (CvHaarClassifierCascade *) cvLoad (filter->profile, 0, 0, 0);
  filter->cvCascade_palm =
      (CvHaarClassifierCascade *) cvLoad (filter->profile_palm, 0, 0, 0);
  if (!filter->cvCascade)
    GST_WARNING ("WARNING: Could not load HAAR classifier cascade: %s.\n",
        filter->profile);
  else
	  GST_INFO ( "Loaded profile %s\n", filter->profile);
  if (!filter->cvCascade_palm)
    GST_WARNING ("WARNING: Could not load HAAR classifier cascade: %s.\n",
        filter->profile_palm);
  else
	  GST_INFO ( "Loaded profile %s\n", filter->profile_palm);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
gst_handdetect_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_handdetect_debug,
      "handdetect",
      0,
      "Detects hand gestures (fist & palm) to support natural-gesture -based media operation.");

  return gst_element_register (plugin, "handdetect", GST_RANK_NONE,
      GST_TYPE_HANDDETECT);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst_handdetect"
#endif

/* gstreamer looks for this structure to register handdetect
 *
 * exchange the string 'Template handdetect' with your handdetect description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "handdetect",
    "Detect hand gestures for media operations",
    gst_handdetect_plugin_init, VERSION, "LGPL", "GStreamer",
    "http://gstreamer.net/")
