#include <stdlib.h>

#include <lcl.h>
#include <raylib.h>

#define RAYLIB_NS "raylib"

#define RAYLIB_COLOR_TYPE_TAG "raylib::color"

#define RAYLIB_BOOL_VOID(fn_name, ray_fn)                               \
  static int fn_name(lcl_interp **interp, int argc, lcl_value **argv,   \
                     lcl_value **out) {                                 \
    (void)interp;                                                       \
    (void)argc;                                                         \
    (void)argv;                                                         \
    if (ray_fn()) {                                                     \
      *out = lcl_int_new(1L);                                           \
    } else {                                                            \
      *out = lcl_int_new(0L);                                           \
    }                                                                   \
    return LCL_RC_OK;                                                   \
  }

#define RAYLIB_VOID_VOID(fn_name, ray_fn)                               \
  static int fn_name(lcl_interp **interp, int argc, lcl_value **argv,   \
                     lcl_value **out) {                                 \
    (void)interp;                                                       \
    (void)argc;                                                         \
    (void)argv;                                                         \
    (void)out;                                                          \
    ray_fn();                                                           \
    return LCL_RC_OK;                                                   \
  }

#define RAYLIB_INT_VOID(fn_name, ray_fn)                                \
  static int fn_name(lcl_interp **interp, int argc, lcl_value **argv,   \
                     lcl_value **out) {                                 \
    (void)interp;                                                       \
    (void)argc;                                                         \
    (void)argv;                                                         \
    *out = lcl_int_new((long)ray_fn());                                 \
    return LCL_RC_OK;                                                   \
  }


#define RAYLIB_INT_INT NULL

RAYLIB_BOOL_VOID(c_raylib_window_should_close, WindowShouldClose)
RAYLIB_BOOL_VOID(c_raylib_is_window_ready, IsWindowReady)
RAYLIB_BOOL_VOID(c_raylib_is_window_fullscreen, IsWindowFullscreen)
RAYLIB_BOOL_VOID(c_raylib_is_window_hidden, IsWindowHidden)
RAYLIB_BOOL_VOID(c_raylib_is_window_maximized, IsWindowMaximized)
RAYLIB_BOOL_VOID(c_raylib_is_window_minimized, IsWindowMinimized)
RAYLIB_BOOL_VOID(c_raylib_is_window_focused, IsWindowFocused)
RAYLIB_BOOL_VOID(c_raylib_is_window_resized, IsWindowResized)

RAYLIB_VOID_VOID(c_raylib_begin_drawing, BeginDrawing)
RAYLIB_VOID_VOID(c_raylib_end_drawing, EndDrawing)
RAYLIB_VOID_VOID(c_raylib_close_window, CloseWindow)
RAYLIB_VOID_VOID(c_raylib_toggle_fullscreen, ToggleFullscreen)
RAYLIB_VOID_VOID(c_raylib_toggle_borderless_windowed, ToggleBorderlessWindowed)
RAYLIB_VOID_VOID(c_raylib_maximize_window, MaximizeWindow)
RAYLIB_VOID_VOID(c_raylib_minimize_window, MinimizeWindow)
RAYLIB_VOID_VOID(c_raylib_set_window_focused, SetWindowFocused)

RAYLIB_INT_VOID(c_raylib_get_screen_width, GetScreenWidth)
RAYLIB_INT_VOID(c_raylib_get_screen_height, GetScreenHeight)
RAYLIB_INT_VOID(c_raylib_get_render_width, GetRenderWidth)
RAYLIB_INT_VOID(c_raylib_get_render_height, GetRenderHeight)
RAYLIB_INT_VOID(c_raylib_get_monitor_count, GetMonitorCount)
RAYLIB_INT_VOID(c_raylib_get_current_monitor, GetCurrentMonitor)

static int c_raylib_color(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  (void)interp;

  long r;
  long g;
  long b;
  long a;
  Color *c;

  if (argc < 4) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &r) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &g) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &b) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[3], &a) != LCL_OK) {
    return LCL_RC_ERR;
  }

  c = (Color *) malloc(sizeof(Color));

  if (!c) {
    return LCL_RC_ERR;
  }

  c->r = r;
  c->g = g;
  c->b = b;
  c->a = a;

  *out = lcl_opaque_new(c, RAYLIB_COLOR_TYPE_TAG, free);

  return LCL_RC_OK;
}

static int c_raylib_init_window(lcl_interp *interp, int argc,
                                lcl_value **argv, lcl_value **out) {
  long width;
  long height;
  const char *title;
  (void) out;

  if (argc < 3) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &width) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &height) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[2], &title) != LCL_OK) {
    return LCL_RC_ERR;
  }

  InitWindow((int)width, (int)height, title);

  return LCL_RC_OK;
}


static int c_raylib_clear_background(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argv;
  (void)out;
  struct Color *c;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], RAYLIB_COLOR_TYPE_TAG, (void**)&c) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  ClearBackground(*c);

  return LCL_RC_OK;
}

static int c_raylib_draw_text(lcl_interp *interp, int argc,
                              lcl_value * *argv, lcl_value **out) {
  (void)interp;
  (void)argv;
  (void)out;
  const char *text;
  long x_pos;
  long y_pos;
  long font_size;
  Color *font_color;

  if (argc < 5) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &text) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &x_pos) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &y_pos) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[3], &font_size) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[4], RAYLIB_COLOR_TYPE_TAG, (void**)&font_color) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  DrawText(text, (int)x_pos, (int)y_pos, (int)font_size, *font_color);

  return LCL_RC_OK;
}

static int c_raylib_set_target_fps(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)out;
  long target_fps;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &target_fps) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  SetTargetFPS((int)target_fps);

  return LCL_RC_OK;
}

static int c_raylib_draw_fps(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  (void)interp;
  (void)out;
  long pos_x;
  long pos_y;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &pos_x) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos_y) != LCL_OK) {
    return LCL_RC_ERR;
  }

  DrawFPS((int)pos_x, (int)pos_y);

  return LCL_RC_OK;
}

static int c_raylib_get_frame_time(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  float fps = GetFrameTime();

  *out = lcl_float_new((double)fps);

  return LCL_RC_OK;
}

void lcl_register_raylib(lcl_interp *interp) {
  lcl_value *raylib_ns = lcl_ns_new(RAYLIB_NS);
  lcl_define_take(interp, RAYLIB_NS, raylib_ns);

  lcl_ns_def(raylib_ns, "init_window",
             lcl_c_proc_new("raylib::init_window", c_raylib_init_window));

  lcl_ns_def(raylib_ns, "close_window",
             lcl_c_proc_new("raylib::close_window", c_raylib_close_window));

  lcl_ns_def(raylib_ns, "window_should_close",
             lcl_c_proc_new("raylib::window_should_close",
                            c_raylib_window_should_close));

  lcl_ns_def(raylib_ns, "begin_drawing",
             lcl_c_proc_new("raylib::begin_drawing", c_raylib_begin_drawing));

  lcl_ns_def(raylib_ns, "end_drawing",
             lcl_c_proc_new("raylib::end_drawing", c_raylib_end_drawing));

  lcl_ns_def(raylib_ns, "is_window_reader",
             lcl_c_proc_new("raylib::is_window_ready", c_raylib_is_window_ready));

  lcl_ns_def(raylib_ns, "is_window_fullscreen",
             lcl_c_proc_new("raylib::is_window_fullscreen",
                            c_raylib_is_window_fullscreen));
  lcl_ns_def(
      raylib_ns, "is_window_hidden",
      lcl_c_proc_new("raylib::is_window_hidden", c_raylib_is_window_hidden));

  lcl_ns_def(raylib_ns, "is_window_maximized",
             lcl_c_proc_new("raylib::is_window_maximized",
                            c_raylib_is_window_maximized));

  lcl_ns_def(raylib_ns, "is_window_minimized",
             lcl_c_proc_new("raylib::is_window_minimized",
                            c_raylib_is_window_minimized));
  lcl_ns_def(
      raylib_ns, "is_window_focused",
      lcl_c_proc_new("raylib::is_window_focused", c_raylib_is_window_focused));

  lcl_ns_def(
      raylib_ns, "is_window_resized",
      lcl_c_proc_new("raylib::is_window_resized", c_raylib_is_window_resized));

  lcl_ns_def(
      raylib_ns, "clear_background",
      lcl_c_proc_new("raylib::clear_background", c_raylib_clear_background));

  lcl_ns_def(raylib_ns, "draw_text",
             lcl_c_proc_new("raylib::draw_text", c_raylib_draw_text));

  lcl_ns_def(raylib_ns, "color",
             lcl_c_proc_new("raylib::color", c_raylib_color));

  lcl_ns_def(raylib_ns, "set_target_fps",
             lcl_c_proc_new("raylib::set_target_fps", c_raylib_set_target_fps));

  lcl_ns_def(raylib_ns, "draw_fps",
             lcl_c_proc_new("raylib::draw_fps", c_raylib_draw_fps));

  lcl_ns_def(raylib_ns, "get_frame_time",
             lcl_c_proc_new("raylib::get_frame_time", c_raylib_get_frame_time));

  lcl_ns_def(
      raylib_ns, "toggle_fullscreen",
      lcl_c_proc_new("raylib::toggle_fullscreen", c_raylib_toggle_fullscreen));

  lcl_ns_def(raylib_ns, "toggle_borderless_windowed",
             lcl_c_proc_new("raylib::toggle_borderless_windowed",
                            c_raylib_toggle_borderless_windowed));

  lcl_ns_def(
      raylib_ns, "maximize_window",
      lcl_c_proc_new("raylib::maximize_window", c_raylib_maximize_window));

  lcl_ns_def(
      raylib_ns, "minimize_window",
      lcl_c_proc_new("raylib::minimize_window", c_raylib_minimize_window));

  lcl_ns_def(raylib_ns, "set_window_focused",
             lcl_c_proc_new("raylib::set_window_focused",
                            c_raylib_set_window_focused));

  lcl_ns_def(
      raylib_ns, "get_screen_width",
      lcl_c_proc_new("raylib::get_screen_width", c_raylib_get_screen_width));

  lcl_ns_def(
      raylib_ns, "get_screen_height",
      lcl_c_proc_new("raylib::get_screen_height", c_raylib_get_screen_height));

  lcl_ns_def(
      raylib_ns, "get_render_width",
      lcl_c_proc_new("raylib::get_render_width", c_raylib_get_render_width));

  lcl_ns_def(
      raylib_ns, "get_render_height",
      lcl_c_proc_new("raylib::get_render_height", c_raylib_get_render_height));
  
  lcl_ns_def(
      raylib_ns, "get_monitor_count",
      lcl_c_proc_new("raylib::get_monitor_count", c_raylib_get_monitor_count));

  lcl_ns_def(raylib_ns, "get_current_monitor",
             lcl_c_proc_new("raylib::get_current_monitor",
                            c_raylib_get_current_monitor));
  
}
