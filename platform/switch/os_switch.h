#include "core/os/os.h"
#include "core/os/input.h"
#include "power_switch.h"
#include "context_gl_switch_egl.h"
#include "servers/visual/visual_server_raster.h"
#include "joypad_switch.h"
#include "audio_driver_switch.h"
#include "main/input_default.h"

class OS_Switch : public OS
{
  int video_driver_index;
  MainLoop *main_loop;
  VideoMode current_videomode;
  VisualServer *visual_server;
  InputDefault *input;
  PowerSwitch *power_manager;
  ContextGLSwitchEGL *gl_context;
  JoypadSwitch *joypad;
  AudioDriverSwitch driver_switch;
  String switch_execpath;

protected:
  virtual void initialize_core();
  virtual Error initialize(const VideoMode &p_desired, int p_video_driver, int p_audio_driver);

  virtual void set_main_loop(MainLoop *p_main_loop);
  virtual void delete_main_loop();

  virtual void finalize();
  virtual void finalize_core();
public:
  virtual bool _check_internal_feature_support(const String &p_feature);

  virtual void alert(const String &p_alert, const String &p_title = "ALERT!");
  virtual String get_stdin_string(bool p_block = true);
  virtual Point2 get_mouse_position() const;
  virtual int get_mouse_button_state() const;
  virtual void set_window_title(const String &p_title);

  virtual void set_video_mode(const VideoMode &p_video_mode, int p_screen);
  virtual VideoMode get_video_mode(int p_screen) const;
  virtual void get_fullscreen_mode_list(List<VideoMode> *p_list, int p_screen) const;

  virtual int get_current_video_driver() const;
  virtual Size2 get_window_size() const;

  virtual Error execute(const String &p_path, const List<String> &p_arguments, bool p_blocking, ProcessID *r_child_id = NULL, String *r_pipe = NULL, int *r_exitcode = NULL, bool read_stderr = false);
  virtual Error kill(const ProcessID &p_pid);

  virtual String get_executable_path() const;
  virtual void set_executable_path(const char *p_execpath);

  virtual bool has_environment(const String &p_var) const;
  virtual String get_environment(const String &p_var) const;
  virtual bool set_environment(const String &p_var, const String &p_value) const;
  virtual String get_name();
  virtual MainLoop *get_main_loop() const;
  virtual Date get_date(bool local = false) const;
  virtual Time get_time(bool local = false) const;
  virtual TimeZoneInfo get_time_zone_info() const;
  virtual void delay_usec(uint32_t p_usec) const;
  virtual uint64_t get_ticks_usec() const;
  virtual bool can_draw() const;
  virtual void set_cursor_shape(CursorShape p_shape);
  virtual void set_custom_mouse_cursor(const RES &p_cursor, CursorShape p_shape, const Vector2 &p_hotspot);

  OS::PowerState get_power_state();
  int get_power_seconds_left();
  int get_power_percent_left();

  void run();
  virtual void swap_buffers();

  OS_Switch();
};
