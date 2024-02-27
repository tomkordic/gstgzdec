#include <gst/gst.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <sys/resource.h>

const char *result_file_path = "test_result.txt";

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = data;

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS:
    g_print("End-of-stream\n");
    g_main_loop_quit(loop);
    break;
  case GST_MESSAGE_ERROR: {
    gchar *debug = NULL;
    GError *err = NULL;

    gst_message_parse_error(msg, &err, &debug);

    g_print("Error: %s\n", err->message);
    g_error_free(err);

    if (debug) {
      g_print("Debug details: %s\n", debug);
      g_free(debug);
    }

    g_main_loop_quit(loop);
    break;
  }
  default:
    break;
  }

  return TRUE;
}

int test_pipeline(const char *input_location) {
  GstStateChangeReturn ret;
  GstElement *pipeline, *filesrc, *gzdec, *sink;
  GMainLoop *loop;
  GstBus *bus;
  guint watch_id;

  loop = g_main_loop_new(NULL, FALSE);

  /* create elements */
  pipeline = gst_pipeline_new("pipeline");

  /* watch for messages on the pipeline's bus (note that this will only
   * work like this when a GLib main loop is running) */
  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  filesrc = gst_element_factory_make("filesrc", "filesource");
  gzdec = gst_element_factory_make("gzdec", "gzdec");
  sink = gst_element_factory_make("filesink", "sink");

  if (!sink) {
    g_print("Filesink not found - check your install\n");
    return -1;
  } else if (!gzdec) {
    g_print("Your self-written filter could not be found. Make sure it "
            "is installed correctly in $(libdir)/gstreamer-1.0/ or "
            "~/.gstreamer-1.0/plugins/ and that gst-inspect-1.0 lists it. "
            "If it doesn't, check with 'GST_DEBUG=*:2 gst-inspect-1.0' for "
            "the reason why it is not being loaded.\n");
    return -1;
  }

  g_object_set(G_OBJECT(filesrc), "location", input_location, NULL);
  g_object_set(G_OBJECT(sink), "location", result_file_path, NULL);

  gst_bin_add_many(GST_BIN(pipeline), filesrc, gzdec, sink, NULL);

  /* link everything together */
  if (!gst_element_link_many(filesrc, gzdec, sink, NULL)) {
    g_print("Failed to link one or more elements!\n");
    return -1;
  }

  /* run */
  ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GstMessage *msg;

    g_print("Failed to start up pipeline!\n");

    /* check if there is an error message with details on the bus */
    msg = gst_bus_poll(bus, GST_MESSAGE_ERROR, 0);
    if (msg) {
      GError *err = NULL;

      gst_message_parse_error(msg, &err, NULL);
      g_print("ERROR: %s\n", err->message);
      g_error_free(err);
      gst_message_unref(msg);
    }
    return -1;
  }

  g_main_loop_run(loop);

  /* clean up */
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  g_source_remove(watch_id);
  g_main_loop_unref(loop);
  return 0;
}

const unsigned char *calculate_md5_checksum(const char *file_path) {
  unsigned char *c =
      (unsigned char *)malloc(MD5_DIGEST_LENGTH * sizeof(unsigned char));
  FILE *in_file = fopen(file_path, "rb");
  MD5_CTX md_context;
  int bytes;
  unsigned char data[1024];

  if (in_file == NULL) {
    printf("%s can't be opened.\n", file_path);
    exit(-3);
  }

  MD5_Init(&md_context);
  while ((bytes = fread(data, 1, 1024, in_file)) != 0)
    MD5_Update(&md_context, data, bytes);
  MD5_Final(c, &md_context);
  fclose(in_file);
  return c;
}

gboolean compare_md5_sums(const unsigned char *sum1,
                          const unsigned char *sum2) {
  int i;
  for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
    if (sum1[i] != sum2[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

void print_checksum(const unsigned char *sum, const char *file_name) {
  int i;
  for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
    printf("%02x", sum[i]);
  }
  printf(" %s\n", file_name);
}

int check_pipeline_results(const char *input_file_path, const char *msg) {
  int result = 0;
  const unsigned char *result_file_md5_sum =
      calculate_md5_checksum(result_file_path);
  const unsigned char *input_md5_sum = calculate_md5_checksum(input_file_path);

  if (!compare_md5_sums(input_md5_sum, result_file_md5_sum)) {
    g_print(msg);
    print_checksum(input_md5_sum, input_file_path);
    print_checksum(result_file_md5_sum, result_file_path);
    result = -4;
  }
  free(input_md5_sum);
  free(result_file_md5_sum);
  return result;
}

void print_mem_usage(const char *leader, const struct rusage *ru) {
  const char *ldr = (leader == NULL) ? "" : leader;

  printf("%sCPU time (secs):         user=%.3f; system=%.3f\n", ldr,
         ru->ru_utime.tv_sec + ru->ru_utime.tv_usec / 1000000.0,
         ru->ru_stime.tv_sec + ru->ru_stime.tv_usec / 1000000.0);
  printf("%sMax resident set size:   %ld\n", ldr, ru->ru_maxrss);
  printf("%sIntegral shared memory:  %ld\n", ldr, ru->ru_ixrss);
  printf("%sIntegral unshared data:  %ld\n", ldr, ru->ru_idrss);
  printf("%sIntegral unshared stack: %ld\n", ldr, ru->ru_isrss);
  printf("%sPage reclaims:           %ld\n", ldr, ru->ru_minflt);
  printf("%sPage faults:             %ld\n", ldr, ru->ru_majflt);
  printf("%sSwaps:                   %ld\n", ldr, ru->ru_nswap);
  printf("%sBlock I/Os:              input=%ld; output=%ld\n", ldr,
         ru->ru_inblock, ru->ru_oublock);
  printf("%sSignals received:        %ld\n", ldr, ru->ru_nsignals);
  printf("%sIPC messages:            sent=%ld; received=%ld\n", ldr,
         ru->ru_msgsnd, ru->ru_msgrcv);
  printf("%sContext switches:        voluntary=%ld; "
         "involuntary=%ld\n",
         ldr, ru->ru_nvcsw, ru->ru_nivcsw);
}

gint main(gint argc, gchar *argv[]) {
  /* initialization */
  gst_init(&argc, &argv);
  if (argc != 2) {
    g_print("Usage: gstgzdec_test file_to_archive\n");
    return 1;
  }

  g_print("DEBUG: creating bz archive\n");
  const char input_file1[1000];
  const char command[1000];
  sprintf(command, "yes y | bzip2 -zk %s", argv[1]);
  if (system(command) == -1) {
    g_print("ERROR: bzip2 not found make sure it is installed on system\n");
    return -2;
  }
  sprintf(input_file1, "%s.bz2", argv[1]);

  g_print("DEBUG: creating zip archive\n");
  sprintf(command, "yes y | gzip -k %s", argv[1]);
  if (system(command) == -1) {
    g_print("ERROR: gzip not found make sure it is installed on system\n");
    return -2;
  }
  const char input_file2[1000];
  sprintf(input_file2, "%s.gz", argv[1]);

  int res = 0;
  struct rusage usage1;
  struct rusage usage2;

  while (1) {
    g_print("DEBUG: getting memory usage before runniong a pipeline\n");
    res = getrusage(RUSAGE_SELF, &usage1);
    print_mem_usage("Before: ", &usage1);
    if (res != 0) {
      g_print("ERROR: failed to get mem usage before running pipelines\n");
      res = -6;
      break;
    } 
    else if (res = test_pipeline(input_file1) != 0) {
      g_print("ERROR: bz2 decode test failed\n");
      break;
    } 
    else if (res = check_pipeline_results(
                         argv[1], "ERROR: bz2 decode file content do "
                                  "not match input file content\n") != 0) {
      break;
    } else if (res = test_pipeline(input_file2) != 0) {
      g_print("ERROR: zip decode test failed\n");
      break;
    } else if (res = check_pipeline_results(
                         argv[1], "ERROR: zip decode file content do "
                                  "not match input file content\n") != 0) {
      break;
    }
    res = getrusage(RUSAGE_SELF, &usage2);
    if (res != 0) {
      g_print("ERROR: failed to get mem usage after running pipelines\n");
      break;
    }
    print_mem_usage("After: ", &usage2);
    if (usage1.ru_maxrss < usage2.ru_maxrss) {
      g_print("ERROR: Memory leak detected, mem before runing test pipelines: %i, memory after: %i\n",
        usage1.ru_maxrss, usage2.ru_maxrss);
        res = -7;
    }
    break;
  }
  remove(input_file1);
  remove(input_file2);
  remove(result_file_path);
  if (res == 0) {
    g_print("DEBUG: ALL tests passed\n");
  } else {
    g_print("ERROR: SOME tests failed\n");
  }
  return res;
}