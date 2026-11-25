// Sail: the engine side of the private control channel to the Sail app (our own code, not part of the port).
// The app starts the engine with one end of a socket pair (SAIL_CTL_FD) and the name of its window group
// (SAIL_GROUP). No TCP port is opened, so no other app can drive the engine.
// Lines in: new 0 <url>, select <id>, close <id> (tabs); load/back/forward/reload/stop/focus/visible (the
// selected tab); key, touch (input from the app); selq, copy, paste <text> (selection and clipboard);
// dlcancel <id>; cleardata (answered with "cleared"); quit.
// Lines out: ready; "t <id> <event>" per tab: new|spare, closed, start, commit, fail, url, title,
// loading, progress, nav; sel <length> <editable>, clip <text>; dl <id> <state> <received> <total> <path>.
// A "spare" tab is an empty one the app may give to its next page.
#ifndef CONTENT_SHELL_BROWSER_SAIL_CONTROL_QNX_H_
#define CONTENT_SHELL_BROWSER_SAIL_CONTROL_QNX_H_

namespace content {
class Shell;
class WebContents;

bool SailEmbedded();                      // true when started by the Sail app
void SailStartControl();                  // UI thread, once
void SailAttach(WebContents* contents);   // report this tab's state from now on
void SailShowShell(Shell* shell);         // show this tab, hide the others (shell_platform_delegate_qnx.cc)
}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_SAIL_CONTROL_QNX_H_
