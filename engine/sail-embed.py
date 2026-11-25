#!/usr/bin/env python3
"""Sail: make the engine run as a child window of the Sail app with a private control channel.
Run inside the build container from /src/cr/src with the native-browser/engine dir mounted at /e:
  python3 /e/sail-embed.py
Idempotent. Everything is switched by environment (SAIL_GROUP, SAIL_WINDOW_ID, SAIL_CTL_FD): without them the
engine behaves as before."""
import os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
def sub(path, old, new, count=1):
    s = open(path).read()
    if new in s: return
    if old not in s: sys.exit("sail-embed: pattern not found in %s:\n%s" % (path, old))
    open(path, "w").write(s.replace(old, new, count)); print("patched", path)

for f in ("sail_control_qnx.h", "sail_control_qnx.cc"):
    shutil.copy(os.path.join(HERE, "sail", f), "content/shell/browser/" + f)

sub("content/shell/BUILD.gn", '      "browser/qnx_berry_daemon_host.cc",\n',
    '      "browser/qnx_berry_daemon_host.cc",\n      "browser/sail_control_qnx.cc",\n      "browser/sail_control_qnx.h",\n')

# --- the window: a child of the app's window group instead of a top-level application window
W = "ui/ozone/platform/qnx_screen/qnx_screen_window.cc"
sub(W, "  int rc = screen_create_window(&window_, ctx_);\n",
    '''  // Sail: started by the Sail app, the page is a child window in the app's window group.
  const char* sail_group = getenv("SAIL_GROUP");
  embedded_ = sail_group && sail_group[0];
  int rc = embedded_ ? screen_create_window_type(&window_, ctx_, SCREEN_CHILD_WINDOW)
                     : screen_create_window(&window_, ctx_);
''')
sub(W, '''  snprintf(group_name_, sizeof(group_name_), "berryshell_%d", getpid());
  if (screen_create_window_group(window_, group_name_) != 0)
    QNX_TRACE_MSG("QNX:OzWin: screen_create_window_group failed\\n");
  screen_set_window_property_cv(window_, SCREEN_PROPERTY_ID_STRING,
                                static_cast<int>(strlen(group_name_)),
                                group_name_);
''', '''  if (embedded_) {
    const char* id = getenv("SAIL_WINDOW_ID");
    if (!id || !id[0])
      id = "sailPage";
    snprintf(group_name_, sizeof(group_name_), "%s", id);
    screen_set_window_property_cv(window_, SCREEN_PROPERTY_ID_STRING,
                                  static_cast<int>(strlen(id)), id);
    if (screen_join_window_group(window_, sail_group) != 0)
      fprintf(stderr, "sail: screen_join_window_group(%s) failed\\n", sail_group);
    else
      fprintf(stderr, "sail: page window joined %s as %s\\n", sail_group, id);
  } else {
  snprintf(group_name_, sizeof(group_name_), "berryshell_%d", getpid());
  if (screen_create_window_group(window_, group_name_) != 0)
    QNX_TRACE_MSG("QNX:OzWin: screen_create_window_group failed\\n");
  screen_set_window_property_cv(window_, SCREEN_PROPERTY_ID_STRING,
                                static_cast<int>(strlen(group_name_)),
                                group_name_);
  }
''')
sub(W, "  ClaimKeyboardFocus();\n\n  // Make sure the window participates",
    "  if (!embedded_)\n    ClaimKeyboardFocus();\n\n  // Make sure the window participates")
sub(W, '''  int zorder = 100;
  screen_set_window_property_iv(window_, SCREEN_PROPERTY_ZORDER, &zorder);

  int visible = 1;
''', '''  int zorder = embedded_ ? 1 : 100;   // Sail: just above the app's own window; the app moves it
  screen_set_window_property_iv(window_, SCREEN_PROPERTY_ZORDER, &zorder);

  int visible = embedded_ ? 0 : 1;    // Sail: the app shows the page window when it has a place for it
''')
# Show/Hide/focus: in the app, visibility and keyboard focus belong to the app
sub(W, '''  visible_ = true;
  if (window_) {
    int visible = 1;
    screen_set_window_property_iv(window_, SCREEN_PROPERTY_VISIBLE, &visible);
    // Re-assert keyboard focus in case becoming visible/active reset it.
    if (!inactive)
      ClaimKeyboardFocus();
''', '''  visible_ = true;
  if (window_) {
    int visible = 1;
    if (!embedded_)
      screen_set_window_property_iv(window_, SCREEN_PROPERTY_VISIBLE, &visible);
    // Re-assert keyboard focus in case becoming visible/active reset it.
    if (!inactive && !embedded_)
      ClaimKeyboardFocus();
''')
sub(W, '''  visible_ = false;
  if (window_) {
    int visible = 0;
    screen_set_window_property_iv(window_, SCREEN_PROPERTY_VISIBLE, &visible);
''', '''  visible_ = false;
  if (window_ && !embedded_) {
    int visible = 0;
    screen_set_window_property_iv(window_, SCREEN_PROPERTY_VISIBLE, &visible);
''')
H = "ui/ozone/platform/qnx_screen/qnx_screen_window.h"
s = open(H).read()
if "embedded_" not in s:
    i = s.index("bool visible_")
    s = s[:i] + "bool embedded_ = false;   // Sail: child window inside the Sail app\n  " + s[i:]
    open(H, "w").write(s); print("patched", H)

# --- events: no Navigator registration when embedded; keys arrive from the app over the control channel
E = "ui/ozone/platform/qnx_screen/qnx_screen_event_source.cc"
sub(E, '''  if (navigator_request_events(0) != BPS_SUCCESS)
    QnxKbdLogf("KBD:navigator_request_events FAILED\\n");
  if (virtualkeyboard_request_events(0) != BPS_SUCCESS)
    QnxKbdLogf("KBD:virtualkeyboard_request_events FAILED\\n");
  if (navigator_set_keyboard_tracking(true) != BPS_SUCCESS)
    QnxKbdLogf("KBD:navigator_set_keyboard_tracking FAILED\\n");
''', '''  g_sail_event_source = this;
  const char* sail_group = getenv("SAIL_GROUP");
  if (!(sail_group && sail_group[0])) {   // Sail: the app owns the Navigator connection
  if (navigator_request_events(0) != BPS_SUCCESS)
    QnxKbdLogf("KBD:navigator_request_events FAILED\\n");
  if (virtualkeyboard_request_events(0) != BPS_SUCCESS)
    QnxKbdLogf("KBD:virtualkeyboard_request_events FAILED\\n");
  if (navigator_set_keyboard_tracking(true) != BPS_SUCCESS)
    QnxKbdLogf("KBD:navigator_set_keyboard_tracking FAILED\\n");
  }
''')
sub(E, "QnxScreenEventSource::QnxScreenEventSource(screen_context_t ctx,\n",
    '''namespace {
QnxScreenEventSource* g_sail_event_source = nullptr;
}

// Sail: a key press forwarded by the app (the hardware keyboard reports to the app's window).
void QnxScreenInjectKey(int sym, int mods, bool down) {
  if (g_sail_event_source)
    g_sail_event_source->ProcessKey(sym, mods, down);
}

QnxScreenEventSource::QnxScreenEventSource(screen_context_t ctx,
''')
sub(E, "QnxScreenEventSource::~QnxScreenEventSource() {\n",
    "QnxScreenEventSource::~QnxScreenEventSource() {\n  if (g_sail_event_source == this)\n    g_sail_event_source = nullptr;\n")
sub(E, '''  bool down = (kflags & SCREEN_FLAG_KEY_DOWN) != 0;

  auto key_cb = GetQnxScreenKeyCallback();
''', '''  bool down = (kflags & SCREEN_FLAG_KEY_DOWN) != 0;
  ProcessKey(sym, mods, down);
}

void QnxScreenEventSource::ProcessKey(int sym, int mods, bool down) {
  auto key_cb = GetQnxScreenKeyCallback();
''')
sub(E, "void QnxScreenEventSource::ProcessTouchEvent(screen_event_t ev, int type) {\n",
    '''// Sail: a finger position from the app (type 0 down, 1 move, 2 up), in page-area pixels. Used for the
// keyboard's touch surface, which reports to the app's window.
void QnxScreenEventSource::InjectTouch(int type, int x, int y, int id) {
  base::MarkQnxProcessPumpActive();
  int rx = x, ry = y;
  QnxScreenMapOutputToRender(x, y, &rx, &ry);
  NotifyQnxScreenTouchGesture(type != 2, type == 1);
  EventType et = type == 0 ? ET_TOUCH_PRESSED : type == 1 ? ET_TOUCH_MOVED : ET_TOUCH_RELEASED;
  PointerDetails details(EventPointerType::kTouch, id, 1.0f, 1.0f, 0.0f);
  TouchEvent touch(et, gfx::Point(rx, ry), base::TimeTicks::Now(), details);
  DispatchEvent(&touch);
}

void QnxScreenInjectTouch(int type, int x, int y, int id) {
  if (g_sail_event_source)
    g_sail_event_source->InjectTouch(type, x, y, id);
}

void QnxScreenEventSource::ProcessTouchEvent(screen_event_t ev, int type) {
''')
EH = "ui/ozone/platform/qnx_screen/qnx_screen_event_source.h"
sub(EH, "  ~QnxScreenEventSource() override;\n",
    "  ~QnxScreenEventSource() override;\n\n  void ProcessKey(int sym, int mods, bool down);   // Sail\n")
sub(EH, "  void ProcessKey(int sym, int mods, bool down);   // Sail\n",
    "  void ProcessKey(int sym, int mods, bool down);   // Sail\n  void InjectTouch(int type, int x, int y, int id);   // Sail\n")
sub(EH, "}  // namespace ui\n\n#endif",
    "void QnxScreenInjectKey(int sym, int mods, bool down);   // Sail\n\n}  // namespace ui\n\n#endif")
sub(EH, "void QnxScreenInjectKey(int sym, int mods, bool down);   // Sail\n",
    "void QnxScreenInjectKey(int sym, int mods, bool down);   // Sail\nvoid QnxScreenInjectTouch(int type, int x, int y, int id);   // Sail\n")

# --- the shell: start the channel, report tabs, leave the Navigator alone
D = "content/shell/browser/shell_platform_delegate_qnx.cc"
sub(D, '#include "content/shell/browser/berry_browser_chrome.h"\n',
    '#include "content/shell/browser/berry_browser_chrome.h"\n#include "content/shell/browser/sail_control_qnx.h"\n')
sub(D, '''  const char* e = getenv("BERRY_APP_MODE");
    return e && e[0] == '1';''', '''  const char* e = getenv("BERRY_APP_MODE");
    return (e && e[0] == '1') || SailEmbedded();''')
sub(D, '  navigator_set_close_prompt("Berry Browser", "Close Berry Browser?");\n',
    '  if (SailEmbedded())\n    SailStartControl();\n  else\n    navigator_set_close_prompt("Berry Browser", "Close Berry Browser?");\n')
sub(D, "  BerryShowShell(shell);\n}\n\nvoid ShellPlatformDelegate::ResizeWebContent",
    "  BerryShowShell(shell);\n  SailAttach(shell->web_contents());\n}\n\nvoid ShellPlatformDelegate::ResizeWebContent")
sub(D, "struct ShellPlatformDelegate::ShellData {",
    "void SailShowShell(Shell* shell) {\n  BerryShowShell(shell);\n}\n\nstruct ShellPlatformDelegate::ShellData {")
# Downloads: the shell has nowhere to save on this platform (the download was dropped). With SAIL_DOWNLOADS set,
# files go there without a prompt, and a name already taken gets " (1)", " (2)".
DL = "content/shell/browser/shell_download_manager_delegate.cc"
sub(DL, "  if (!download->GetForcedFilePath().empty()) {",
    """  if (const char* sail_dir = getenv("SAIL_DOWNLOADS"); sail_dir && sail_dir[0]) {
    default_download_path_ = base::FilePath(sail_dir);
    suppress_prompting_ = true;
  }

  if (!download->GetForcedFilePath().empty()) {""")
sub(DL, "  base::FilePath suggested_path(suggested_directory.Append(generated_name));",
    "  base::FilePath suggested_path(suggested_directory.Append(generated_name));\n  suggested_path = base::GetUniquePath(suggested_path);   // Sail")
sub(DL, '#include "content/shell/browser/shell_download_manager_delegate.h"\n',
    '#include "content/shell/browser/shell_download_manager_delegate.h"\n\n#include <stdlib.h>\n')

# Selecting text on a page crashed the engine: the clipboard said it had a selection buffer (as on Linux) but
# only creates one on Linux and Fuchsia, so the first selection tripped a CHECK in flat_map.
sub("ui/base/clipboard/clipboard_non_backed.cc",
    "#if BUILDFLAG(IS_CHROMEOS_ASH)\n  return false;\n#else\n  return true;\n#endif\n}\n#endif  // BUILDFLAG(IS_OZONE)",
    "  return internal_clipboards_.contains(ClipboardBuffer::kSelection);   // Sail: only if it was created\n}\n#endif  // BUILDFLAG(IS_OZONE)")
print("sail-embed: done")
