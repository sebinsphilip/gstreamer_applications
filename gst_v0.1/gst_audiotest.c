#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <glib.h>
#include <time.h>
#include <sys/time.h>

typedef struct
{
    GstElement *pipeline;
    GstElement *audio_decoder;
    GMainLoop *player_loop;
} GstData;

static gboolean bus_call (GstBus     *bus,
                          GstMessage *msg,
                          gpointer    data)
{
    GstData *pGstData = (GstData *) data;

    switch (GST_MESSAGE_TYPE (msg)) {

        case GST_MESSAGE_EOS:
#if 1
            g_print ("End of stream\n");
            g_main_loop_quit (pGstData->player_loop);
            break;
#else
            g_print ("End of stream....Starting Again\n");
            /* Again playing same file starting from 1 second */
            if (!gst_element_seek(pGstData->pipeline,
                        1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                        GST_SEEK_TYPE_SET,  1 * GST_SECOND,
                        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
            {
                g_print ("Seek failed!\n");
            }
#endif
            break;

        case GST_MESSAGE_ERROR:
            {
                gchar  *debug;
                GError *error;

                gst_message_parse_error (msg, &error, &debug);
                g_free (debug);

                g_printerr ("Error: %s\n", error->message);
                g_error_free (error);

                g_main_loop_quit (pGstData->player_loop);
                break;
            }
        default:
            break;
    }

    return TRUE;
}


static void on_pad_added (GstElement *element,
                          GstPad     *pad,
                          gpointer    data)
{
    GstPad *sinkpad;
    GstElement *decoder = (GstElement *) data;
    //GstElement *queue;
    //queue  = gst_element_factory_make ("queue",      "queue");


    /* We can now link this pad with the vorbis-decoder sink pad */
    g_print ("Dynamic pad created, linking demuxer/decoder\n");

    sinkpad = gst_element_get_static_pad (decoder, "sink");

    gst_pad_link (pad, sinkpad);

    gst_object_unref (sinkpad);
}


/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, GstData *data)
{
    gchar *str = NULL;
    GstState state = GST_STATE_NULL;

    if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) != G_IO_STATUS_NORMAL)
    {
        return TRUE;
    }

    switch (g_ascii_tolower (str[0]))
    {
        case 'p':
            g_print ("Getting state ...\n");
            gst_element_get_state (data->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
            g_print ("gst_element_get_state() returned -> Got state = %s\n\n", (GST_STATE_PLAYING==state) ? "PLAYING" : ((GST_STATE_PAUSED==state) ? "PAUSED" : "OTHER"));

            g_print ("Setting state to %s\n", (GST_STATE_PLAYING==state) ? "PAUSED" : ((GST_STATE_PAUSED==state) ? "PLAYING" : "NULL"));
            gst_element_set_state (data->pipeline, (GST_STATE_PLAYING==state) ? GST_STATE_PAUSED : ((GST_STATE_PAUSED==state) ? GST_STATE_PLAYING : GST_STATE_NULL));
            g_print ("gst_element_set_state() returned.\n\n");
            break;

        case 'q':
            g_print ("Calling g_main_loop_quit() ...\n");
            g_main_loop_quit (data->player_loop);
            g_print ("g_main_loop_quit() returned.\n\n");
            break;

        case '\n':
            /* Ignore */
            break;

        default:
            g_print ("Invalid key input - '%c'\n", str[0]);
            break;
    }

    g_free (str);

    return TRUE;
}

#define SECOND_TO_MICROSEC  1000000
#define MILLISEC_TO_NANOSEC 1000000
#define NANOSEC_TO_MILLISEC 1000000
void* get_elapsed_time (void *arg)
{
    GstElement *pipeline = (GstElement *)arg;
    GstFormat fmt = GST_FORMAT_TIME;
    gint64 pos, len = 0;
    gint timeinms = -1;
    struct timeval start, end;
    GstState state = GST_STATE_NULL;
    GstStateChangeReturn gResult;
    int i = 0;
    gint64 lLBAOffset = 0;

    while (1)
    {
        gettimeofday(&start, NULL);
        for (i = 0; i < 50/*MAX_STATE_CHANGE_QUERY_RETRIES*/; i++)
        {
            /* get pipeline status in 2 msec */
            gResult = gst_element_get_state (pipeline, &state, NULL,
                    2 * MILLISEC_TO_NANOSEC);
            //g_print ("gst_element_get_status(%d)=%d return val=%d\n", i, state, gResult);
            if (GST_STATE_CHANGE_ASYNC != gResult)
            {
                break;
            }
        }

        if ((state != GST_STATE_PLAYING) /*&&
                (state != GST_STATE_PAUSED)*/)
        {
            //g_print ("Cannot get duration when pipeline is not playing\n");
            continue;
        }

        /* get total duration - one-time */
        if (0 == len)
        {
            gst_element_query_duration (pipeline, GST_FORMAT_TIME, &len);
            len = len / NANOSEC_TO_MILLISEC;
        }

        if (gst_element_query_position (pipeline, fmt, &pos))
        {
            timeinms = pos / NANOSEC_TO_MILLISEC;
            //g_print ("Playing at: %d/%ld (ms)\n", timeinms, len);
        }
        else
        {
            g_print ("ERROR:: gst_element_query_position failed!\n");
            continue;
        }

        if (gst_element_query_convert(pipeline, GST_FORMAT_TIME, (gint64)pos,
                                      fmt, &lLBAOffset))
            {
                //g_print ("Position in LBA offset %#lx\n", lLBAOffset);
            }
        else
        {
            g_print ("gst_element_query_convert() failed\n");
        }

        gettimeofday(&end, NULL);
        //g_print ("---> Query took %ld usec\n", ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));
        g_print ("Elapsed = %8d ms, Total = %8ld ms, LBA = %#lx, QueryDelay = %8ld usec\n", timeinms, len, lLBAOffset,
                 (((end.tv_sec * SECOND_TO_MICROSEC) + end.tv_usec) - ((start.tv_sec * SECOND_TO_MICROSEC) + start.tv_usec)));
        sleep (1);
    }
}

int main (int argc, char *argv[])
{
    int err;
    GMainLoop *loop;
    GstBin *pipe;

    GstElement *pipeline, *source, *demuxer, *decoder, *conv, *sink, *parser, *resampler, *queue;
    GstBus *bus;
    guint bus_watch_id;

    GIOChannel *io_stdin;
    GstData gstreamerData;

    pthread_t tid;
    memset (&gstreamerData, 0, sizeof(GstData));

    /* Initialisation */
    gst_init (&argc, &argv);

    loop = g_main_loop_new (NULL, FALSE);


    /* Check input arguments */
    if (argc < 2) {
        g_printerr ("Usage: %s <Ogg/Vorbis_filename> <usbmediaplug|alsasink>\n", argv[0]);
        return -1;
    }


    /* Create gstreamer elements */
    pipeline = gst_pipeline_new ("audio-player");
    source   = gst_element_factory_make ("filesrc",       "file-source");
    demuxer  = gst_element_factory_make ("oggdemux",      "ogg-demuxer");
    //queue  = gst_element_factory_make ("queue",      "queue");
    decoder  = gst_element_factory_make ("vorbisdec",     "vorbis-decoder");
    //decoder  = gst_element_factory_make ("avdec_mp3",     "mp3-decoder");
    //parser  = gst_element_factory_make ("mpegaudioparse",     "audio-parse");
    resampler  = gst_element_factory_make ("audioresample",     "audio-resample");
    conv     = gst_element_factory_make ("audioconvert",  "converter");
#ifdef PHOENIX
    g_print ("using alsasink\n");
    sink     = gst_element_factory_make ("alsasink", "audio-output");
#else
    sink     = gst_element_factory_make ("autoaudiosink", "audio-output");
#endif

    if (!pipeline || !source || !demuxer || !decoder || !conv || !sink) {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }
    g_print ("pipeline and elements are created\n");
    gstreamerData.player_loop = loop;
    gstreamerData.pipeline = pipeline;
    gstreamerData.audio_decoder = decoder;

    /* Set up the pipeline */

    /* we set the input filename to the source element */
    g_object_set (G_OBJECT (source), "location", argv[1], NULL);

#ifdef PHOENIX
    if ((argc > 2) && (!strcmp (argv[2], "usbmediaplug")))
    {
        /* set usbmediaplug device for alsasink */
        g_print ("setting device name to usbmediaplug for alsasink\n");
        g_object_set (G_OBJECT (sink), "device", "usbmediaplug", NULL);
    }
#endif
    g_print ("element configs are set\n");

    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    g_print ("element configs are set1\n");
    bus_watch_id = gst_bus_add_watch (bus, bus_call, &gstreamerData);
    g_print ("element configs are set2\n");
    gst_object_unref (bus);

    /* we add all elements into the pipeline */
    /* file-source | ogg-demuxer | vorbis-decoder | converter | alsa-output */
    g_print ("element configs are set3\n");
    gst_bin_add_many (GST_BIN (pipeline),
            source, demuxer , decoder,resampler, conv, sink, NULL);
    g_print ("added elements to pipeline\n");

    /* we link the elements together */
    /* file-source -> ogg-demuxer ~> vorbis-decoder -> converter -> alsa-output */
    gst_element_link (source, demuxer);
    gst_element_link_many (decoder,resampler, conv, sink, NULL);
    g_signal_connect (demuxer, "pad-added", G_CALLBACK (on_pad_added), decoder);
    g_print ("linked elements in pipeline\n");
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
    // GST_DEBUG_DUMP_DOT_DIR=. ./application

    /* note that the demuxer will be linked to the decoder dynamically.
     * The reason is that Ogg may contain various streams (for example
     * audio and video). The source pad(s) will be created at run time,
     * by the demuxer when it detects the amount and nature of streams.
     * Therefore we connect a callback function which will be executed
     * when the "pad-added" is emitted.*/


    /* Set the pipeline to "playing" state*/
    g_print ("Now playing: %s\n", argv[1]);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    /* Print usage map */
    g_print (
            "USAGE: Choose one of the following options, then press enter:\n"
            " 'P' to toggle between PAUSE and PLAY\n"
            " 'Q' to quit\n");

    io_stdin = g_io_channel_unix_new (fileno (stdin));
    g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &gstreamerData);

    /* create thread for time position query */
    err = pthread_create (&tid, NULL, &get_elapsed_time, (void *)pipeline);
    if (0 != err)
        g_print ("Error in creating thread to query time pos");
    else
        g_print ("Thread to query time created successfully");

    /* Iterate */
    g_print ("Running...\n");
    g_main_loop_run (loop);


    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);

    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);

    return 0;
}

