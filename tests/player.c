/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* TODO:
 * - start with pause, go to playing
 * - play, pause, play
 * - set uri in play/pause
 * - play/pause after eos
 * - seek in play/pause/stopped, after eos, back to 0, after duration
 * - http buffering
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <check.h>

#define fail_unless_equals_int(a, b)                                    \
G_STMT_START {                                                          \
  int first = a;                                                        \
  int second = b;                                                       \
  fail_unless(first == second,                                          \
    "'" #a "' (%d) is not equal to '" #b"' (%d)", first, second);       \
} G_STMT_END;

#define fail_unless_equals_uint64(a, b)                                 \
G_STMT_START {                                                          \
  guint64 first = a;                                                    \
  guint64 second = b;                                                   \
  fail_unless(first == second,                                          \
    "'" #a "' (%" G_GUINT64_FORMAT ") is not equal to '" #b"' (%"       \
    G_GUINT64_FORMAT ")", first, second);                               \
} G_STMT_END;

#include <gst/player/gstplayer.h>

GST_DEBUG_CATEGORY_STATIC (test_debug);
#define GST_CAT_DEFAULT test_debug

START_TEST (test_create_and_free)
{
  GstPlayer *player;

  player = gst_player_new ();
  fail_unless (player != NULL);
  g_object_unref (player);
}

END_TEST;

START_TEST (test_set_and_get_uri)
{
  GstPlayer *player;
  gchar *uri;

  player = gst_player_new ();

  fail_unless (player != NULL);

  gst_player_set_uri (player, "file:///path/to/a/file");
  uri = gst_player_get_uri (player);

  fail_unless (g_strcmp0 (uri, "file:///path/to/a/file") == 0);

  g_free (uri);
  g_object_unref (player);
}

END_TEST;

typedef enum
{
  STATE_CHANGE_BUFFERING,
  STATE_CHANGE_DURATION_CHANGED,
  STATE_CHANGE_END_OF_STREAM,
  STATE_CHANGE_ERROR,
  STATE_CHANGE_POSITION_UPDATED,
  STATE_CHANGE_STATE_CHANGED,
  STATE_CHANGE_VIDEO_DIMENSIONS_CHANGED,
} TestPlayerStateChange;

static const gchar *
test_player_state_change_get_name (TestPlayerStateChange change)
{
  switch (change) {
    case STATE_CHANGE_BUFFERING:
      return "buffering";
    case STATE_CHANGE_DURATION_CHANGED:
      return "duration-changed";
    case STATE_CHANGE_END_OF_STREAM:
      return "end-of-stream";
    case STATE_CHANGE_ERROR:
      return "error";
    case STATE_CHANGE_POSITION_UPDATED:
      return "position-updated";
    case STATE_CHANGE_STATE_CHANGED:
      return "state-changed";
    case STATE_CHANGE_VIDEO_DIMENSIONS_CHANGED:
      return "video-dimensions-changed";
    default:
      g_assert_not_reached ();
      break;
  }
}

typedef struct _TestPlayerState TestPlayerState;
struct _TestPlayerState
{
  GMainLoop *loop;

  gint buffering_percent;
  guint64 position, duration;
  gboolean end_of_stream, error;
  GstPlayerState state;
  gint width, height;

  void (*test_callback) (GstPlayer * player, TestPlayerStateChange change,
      TestPlayerState * old_state, TestPlayerState * new_state);
  gpointer test_data;
};

static void
test_player_state_change_debug (GstPlayer * player,
    TestPlayerStateChange change, TestPlayerState * old_state,
    TestPlayerState * new_state)
{
  GST_DEBUG_OBJECT (player, "Changed %s:\n"
      "\tbuffering %d%% -> %d%%\n"
      "\tposition %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT "\n"
      "\tduration %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT "\n"
      "\tend-of-stream %d -> %d\n"
      "\terror %d -> %d\n"
      "\tstate %s -> %s\n"
      "\twidth/height %d/%d -> %d/%d",
      test_player_state_change_get_name (change),
      old_state->buffering_percent, new_state->buffering_percent,
      GST_TIME_ARGS (old_state->position), GST_TIME_ARGS (new_state->position),
      GST_TIME_ARGS (old_state->duration), GST_TIME_ARGS (new_state->duration),
      old_state->end_of_stream, new_state->end_of_stream,
      old_state->error, new_state->error,
      gst_player_state_get_name (old_state->state),
      gst_player_state_get_name (new_state->state), old_state->width,
      old_state->height, new_state->width, new_state->height);
}

static void
test_player_state_reset (TestPlayerState * state)
{
  state->buffering_percent = 100;
  state->position = state->duration = -1;
  state->end_of_stream = state->error = FALSE;
  state->state = GST_PLAYER_STATE_STOPPED;
  state->width = state->height = 0;
}

static void
buffering_cb (GstPlayer * player, gint percent, TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->buffering_percent = percent;
  test_player_state_change_debug (player, STATE_CHANGE_BUFFERING, &old_state,
      state);
  state->test_callback (player, STATE_CHANGE_BUFFERING, &old_state, state);
}

static void
duration_changed_cb (GstPlayer * player, guint64 duration,
    TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->duration = duration;
  test_player_state_change_debug (player, STATE_CHANGE_DURATION_CHANGED,
      &old_state, state);
  state->test_callback (player, STATE_CHANGE_DURATION_CHANGED, &old_state,
      state);
}

static void
end_of_stream_cb (GstPlayer * player, TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->end_of_stream = TRUE;
  test_player_state_change_debug (player, STATE_CHANGE_END_OF_STREAM,
      &old_state, state);
  state->test_callback (player, STATE_CHANGE_END_OF_STREAM, &old_state, state);
}

static void
error_cb (GstPlayer * player, GError * error, TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->error = TRUE;
  test_player_state_change_debug (player, STATE_CHANGE_ERROR, &old_state,
      state);
  state->test_callback (player, STATE_CHANGE_ERROR, &old_state, state);
}

static void
position_updated_cb (GstPlayer * player, guint64 position,
    TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->position = position;
  test_player_state_change_debug (player, STATE_CHANGE_POSITION_UPDATED,
      &old_state, state);
  state->test_callback (player, STATE_CHANGE_POSITION_UPDATED, &old_state,
      state);
}

static void
state_changed_cb (GstPlayer * player, GstPlayerState player_state,
    TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->state = player_state;

  if (player_state == GST_PLAYER_STATE_STOPPED)
    test_player_state_reset (state);

  test_player_state_change_debug (player, STATE_CHANGE_STATE_CHANGED,
      &old_state, state);
  state->test_callback (player, STATE_CHANGE_STATE_CHANGED, &old_state, state);
}

static void
video_dimensions_changed_cb (GstPlayer * player, gint width, gint height,
    TestPlayerState * state)
{
  TestPlayerState old_state = *state;

  state->width = width;
  state->height = height;
  test_player_state_change_debug (player, STATE_CHANGE_VIDEO_DIMENSIONS_CHANGED,
      &old_state, state);
  state->test_callback (player, STATE_CHANGE_VIDEO_DIMENSIONS_CHANGED,
      &old_state, state);
}

static GstPlayer *
test_player_new (TestPlayerState * state)
{
  GstPlayer *player;
  GstElement *playbin, *fakesink;

  player = gst_player_new ();
  fail_unless (player != NULL);

  test_player_state_reset (state);

  playbin = gst_player_get_pipeline (player);
  fakesink = gst_element_factory_make ("fakesink", "audio-sink");
  g_object_set (fakesink, "sync", TRUE, NULL);
  g_object_set (playbin, "audio-sink", fakesink, NULL);
  fakesink = gst_element_factory_make ("fakesink", "video-sink");
  g_object_set (fakesink, "sync", TRUE, NULL);
  g_object_set (playbin, "video-sink", fakesink, NULL);
  gst_object_unref (playbin);

  g_object_set (player, "dispatch-to-main-context", TRUE, NULL);
  g_signal_connect (player, "buffering", G_CALLBACK (buffering_cb), state);
  g_signal_connect (player, "duration-changed",
      G_CALLBACK (duration_changed_cb), state);
  g_signal_connect (player, "end-of-stream", G_CALLBACK (end_of_stream_cb),
      state);
  g_signal_connect (player, "error", G_CALLBACK (error_cb), state);
  g_signal_connect (player, "position-updated",
      G_CALLBACK (position_updated_cb), state);
  g_signal_connect (player, "state-changed", G_CALLBACK (state_changed_cb),
      state);
  g_signal_connect (player, "video-dimensions-changed",
      G_CALLBACK (video_dimensions_changed_cb), state);

  return player;
}

static void
test_play_audio_video_eos_cb (GstPlayer * player, TestPlayerStateChange change,
    TestPlayerState * old_state, TestPlayerState * new_state)
{
  gint step = GPOINTER_TO_INT (new_state->test_data);
  gboolean video;

  video = ! !(step & 0x10);
  step = (step & (~0x10));

  switch (step) {
    case 0:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_STOPPED);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_BUFFERING);
      new_state->test_data =
          GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      break;
    case 1:
      fail_unless_equals_int (change, STATE_CHANGE_VIDEO_DIMENSIONS_CHANGED);
      if (video) {
        fail_unless_equals_int (new_state->width, 320);
        fail_unless_equals_int (new_state->height, 240);
      } else {
        fail_unless_equals_int (new_state->width, 0);
        fail_unless_equals_int (new_state->height, 0);
      }
      new_state->test_data =
          GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      break;
    case 2:
      fail_unless_equals_int (change, STATE_CHANGE_DURATION_CHANGED);
      fail_unless_equals_uint64 (new_state->duration,
          G_GUINT64_CONSTANT (464399092));
      new_state->test_data =
          GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      break;
    case 3:
      fail_unless_equals_int (change, STATE_CHANGE_POSITION_UPDATED);
      fail_unless_equals_uint64 (new_state->position, G_GUINT64_CONSTANT (0));
      new_state->test_data =
          GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      break;
    case 4:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_BUFFERING);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_PLAYING);
      new_state->test_data =
          GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      break;
    case 5:
      if (change == STATE_CHANGE_POSITION_UPDATED) {
        fail_unless (old_state->position <= new_state->position);
      } else {
        fail_unless_equals_uint64 (old_state->position, old_state->duration);
        fail_unless_equals_int (change, STATE_CHANGE_END_OF_STREAM);
        new_state->test_data =
            GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      }
      break;
    case 6:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_PLAYING);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_STOPPED);
      new_state->test_data =
          GINT_TO_POINTER ((video ? 0x10 : 0x00) | (step + 1));
      g_main_loop_quit (new_state->loop);
      break;
    default:
      fail ();
      break;
  }
}

START_TEST (test_play_audio_eos)
{
  GstPlayer *player;
  TestPlayerState state;
  gchar *uri;

  memset (&state, 0, sizeof (state));
  state.loop = g_main_loop_new (NULL, FALSE);
  state.test_callback = test_play_audio_video_eos_cb;
  state.test_data = GINT_TO_POINTER (0);

  player = test_player_new (&state);

  fail_unless (player != NULL);

  uri = gst_filename_to_uri (TEST_PATH "/audio-short.ogg", NULL);
  fail_unless (uri != NULL);
  gst_player_set_uri (player, uri);
  g_free (uri);

  gst_player_play (player);
  g_main_loop_run (state.loop);

  fail_unless_equals_int (GPOINTER_TO_INT (state.test_data), 7);

  g_object_unref (player);
  g_main_loop_unref (state.loop);
}

END_TEST;

START_TEST (test_play_audio_video_eos)
{
  GstPlayer *player;
  TestPlayerState state;
  gchar *uri;

  memset (&state, 0, sizeof (state));
  state.loop = g_main_loop_new (NULL, FALSE);
  state.test_callback = test_play_audio_video_eos_cb;
  state.test_data = GINT_TO_POINTER (0x10);

  player = test_player_new (&state);

  fail_unless (player != NULL);

  uri = gst_filename_to_uri (TEST_PATH "/audio-video-short.ogg", NULL);
  fail_unless (uri != NULL);
  gst_player_set_uri (player, uri);
  g_free (uri);

  gst_player_play (player);
  g_main_loop_run (state.loop);

  fail_unless_equals_int (GPOINTER_TO_INT (state.test_data) & (~0x10), 7);

  g_object_unref (player);
  g_main_loop_unref (state.loop);
}

END_TEST;

static void
test_play_error_invalid_uri_cb (GstPlayer * player,
    TestPlayerStateChange change, TestPlayerState * old_state,
    TestPlayerState * new_state)
{
  gint step = GPOINTER_TO_INT (new_state->test_data);

  switch (step) {
    case 0:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_STOPPED);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_BUFFERING);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 1:
      fail_unless_equals_int (change, STATE_CHANGE_ERROR);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 2:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_BUFFERING);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_STOPPED);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      g_main_loop_quit (new_state->loop);
      break;
    default:
      fail ();
      break;
  }
}

START_TEST (test_play_error_invalid_uri)
{
  GstPlayer *player;
  TestPlayerState state;

  memset (&state, 0, sizeof (state));
  state.loop = g_main_loop_new (NULL, FALSE);
  state.test_callback = test_play_error_invalid_uri_cb;
  state.test_data = GINT_TO_POINTER (0);

  player = test_player_new (&state);

  fail_unless (player != NULL);

  gst_player_set_uri (player, "foo://bar");

  gst_player_play (player);
  g_main_loop_run (state.loop);

  fail_unless_equals_int (GPOINTER_TO_INT (state.test_data), 3);

  g_object_unref (player);
  g_main_loop_unref (state.loop);
}

END_TEST;

static void
test_play_error_invalid_uri_and_play_cb (GstPlayer * player,
    TestPlayerStateChange change, TestPlayerState * old_state,
    TestPlayerState * new_state)
{
  gint step = GPOINTER_TO_INT (new_state->test_data);
  gchar *uri;

  switch (step) {
    case 0:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_STOPPED);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_BUFFERING);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 1:
      fail_unless_equals_int (change, STATE_CHANGE_ERROR);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 2:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_BUFFERING);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_STOPPED);
      new_state->test_data = GINT_TO_POINTER (step + 1);

      uri = gst_filename_to_uri (TEST_PATH "/audio-short.ogg", NULL);
      fail_unless (uri != NULL);
      gst_player_set_uri (player, uri);
      g_free (uri);

      gst_player_play (player);
      break;
    case 3:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_STOPPED);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_BUFFERING);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 4:
      fail_unless_equals_int (change, STATE_CHANGE_VIDEO_DIMENSIONS_CHANGED);
      fail_unless_equals_int (new_state->width, 0);
      fail_unless_equals_int (new_state->height, 0);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 5:
      fail_unless_equals_int (change, STATE_CHANGE_DURATION_CHANGED);
      fail_unless_equals_uint64 (new_state->duration,
          G_GUINT64_CONSTANT (464399092));
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 6:
      fail_unless_equals_int (change, STATE_CHANGE_POSITION_UPDATED);
      fail_unless_equals_uint64 (new_state->position, G_GUINT64_CONSTANT (0));
      new_state->test_data = GINT_TO_POINTER (step + 1);
      break;
    case 7:
      fail_unless_equals_int (change, STATE_CHANGE_STATE_CHANGED);
      fail_unless_equals_int (old_state->state, GST_PLAYER_STATE_BUFFERING);
      fail_unless_equals_int (new_state->state, GST_PLAYER_STATE_PLAYING);
      new_state->test_data = GINT_TO_POINTER (step + 1);
      g_main_loop_quit (new_state->loop);
      break;
    default:
      fail ();
      break;
  }
}

START_TEST (test_play_error_invalid_uri_and_play)
{
  GstPlayer *player;
  TestPlayerState state;

  memset (&state, 0, sizeof (state));
  state.loop = g_main_loop_new (NULL, FALSE);
  state.test_callback = test_play_error_invalid_uri_and_play_cb;
  state.test_data = GINT_TO_POINTER (0);

  player = test_player_new (&state);

  fail_unless (player != NULL);

  gst_player_set_uri (player, "foo://bar");

  gst_player_play (player);
  g_main_loop_run (state.loop);

  fail_unless_equals_int (GPOINTER_TO_INT (state.test_data), 8);

  g_object_unref (player);
  g_main_loop_unref (state.loop);
}

END_TEST;

static Suite *
player_suite (void)
{
  Suite *s = suite_create ("GstPlayer");

  TCase *tc_general = tcase_create ("general");

  tcase_set_timeout (tc_general, 120);

  tcase_add_test (tc_general, test_create_and_free);
  tcase_add_test (tc_general, test_set_and_get_uri);
  tcase_add_test (tc_general, test_play_audio_eos);
  tcase_add_test (tc_general, test_play_audio_video_eos);
  tcase_add_test (tc_general, test_play_error_invalid_uri);
  tcase_add_test (tc_general, test_play_error_invalid_uri_and_play);

  suite_add_tcase (s, tc_general);

  return s;
}

int
main (int argc, char **argv)
{
  int number_failed;
  Suite *s;
  SRunner *sr;

  gst_init (NULL, NULL);

  GST_DEBUG_CATEGORY_INIT (test_debug, "test", 0, "GstPlayer test");

  s = player_suite ();
  sr = srunner_create (s);

  srunner_run_all (sr, CK_NORMAL);

  number_failed = srunner_ntests_failed (sr);

  srunner_free (sr);
  return (number_failed == 0) ? 0 : -1;
}
