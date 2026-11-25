// Sail: control channel between the Sail app and the engine. See sail_control_qnx.h.
#include "content/shell/browser/sail_control_qnx.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/qnx_hard_watchdog.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/isolated_world_ids.h"
#include "base/json/string_escape.h"
#include "base/values.h"
#include "base/functional/callback_helpers.h"
#include "content/public/common/referrer.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/shell_browser_context.h"
#include "content/shell/browser/shell_content_browser_client.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/size.h"
#include "ui/ozone/platform/qnx_screen/qnx_screen_event_source.h"
#include "url/gurl.h"

namespace content {

namespace {

int g_fd = -1;
bool g_quitting = false;
bool g_ready = false;   // the engine's own first (blank) load is over; loads from the app are safe from here

void Emit(const std::string& line) {
  if (g_fd < 0)
    return;
  std::string s = line;
  for (char& c : s) {
    if (c == '\n' || c == '\r')
      c = ' ';
  }
  s.push_back('\n');
  size_t off = 0;
  while (off < s.size()) {
    ssize_t n = write(g_fd, s.data() + off, s.size() - off);
    if (n <= 0)
      return;
    off += static_cast<size_t>(n);
  }
}

// Text that may hold line breaks travels on one line: "\\n" for a line break, "\\\\" for a backslash.
std::string Escape(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c != '\r')
      out.push_back(c);
  }
  return out;
}

std::string Unescape(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\\' && i + 1 < in.size()) {
      ++i;
      out.push_back(in[i] == 'n' ? '\n' : in[i]);
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

// Downloads: every change of a download is reported as "dl <id> <run|done|fail|cancel> <received> <total> <path>".
// Progress is reported in steps of a twentieth of the file (or 512 KB), not on every chunk.
class SailDownloads : public DownloadManager::Observer,
                      public download::DownloadItem::Observer {
 public:
  void Watch(DownloadManager* manager) {
    if (manager_ || !manager)
      return;
    manager_ = manager;
    manager->AddObserver(this);
  }
  void Cancel(uint32_t id) {
    download::DownloadItem* item = manager_ ? manager_->GetDownload(id) : nullptr;
    if (item)
      item->Cancel(true);
  }

  // DownloadManager::Observer:
  void OnDownloadCreated(DownloadManager* manager,
                         download::DownloadItem* item) override {
    item->AddObserver(this);
    Report(item);
  }
  void ManagerGoingDown(DownloadManager* manager) override {
    manager->RemoveObserver(this);
    manager_ = nullptr;
  }

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override { Report(item); }
  void OnDownloadDestroyed(download::DownloadItem* item) override {
    item->RemoveObserver(this);
    last_.erase(item->GetId());
  }

 private:
  void Report(download::DownloadItem* item) {
    const char* state = "run";
    switch (item->GetState()) {
      case download::DownloadItem::COMPLETE:
        state = "done";
        break;
      case download::DownloadItem::CANCELLED:
        state = "cancel";
        break;
      case download::DownloadItem::INTERRUPTED:
        state = "fail";
        break;
      default:
        break;
    }
    const std::string path = item->GetTargetFilePath().value();
    if (path.empty() && item->GetState() == download::DownloadItem::IN_PROGRESS)
      return;                                   // no name yet
    const int64_t got = item->GetReceivedBytes();
    const int64_t total = item->GetTotalBytes();
    std::string key = std::string(state) + path;
    Last& last = last_[item->GetId()];
    const int64_t step = std::max<int64_t>(total / 20, 512 * 1024);
    if (key == last.key && got - last.got < step)
      return;
    last.key = key;
    last.got = got;
    Emit(base::StringPrintf("dl %u %s %lld %lld ", item->GetId(), state,
                            static_cast<long long>(got),
                            static_cast<long long>(total)) + path);
  }

  struct Last {
    std::string key;
    int64_t got = 0;
  };
  raw_ptr<DownloadManager> manager_ = nullptr;
  std::map<uint32_t, Last> last_;
};

// Run a line of script in the frame that has the focus, in a world of its own (the page's scripts cannot see or
// change it), and hand back its value. Used for the selection and for inserting pasted text.
void AskPage(WebContents* wc,
             const std::u16string& script,
             base::OnceCallback<void(base::Value)> done) {
  RenderFrameHost* frame = wc->GetFocusedFrame();
  if (!frame)
    frame = wc->GetPrimaryMainFrame();
  if (!frame)
    return;
  frame->ExecuteJavaScriptInIsolatedWorld(script, std::move(done),
                                          ISOLATED_WORLD_ID_CONTENT_END);
}

SailDownloads& Downloads() {
  static SailDownloads* d = new SailDownloads();
  return *d;
}

class SailTab;
std::vector<SailTab*>& Tabs() {
  static std::vector<SailTab*> tabs;
  return tabs;
}
int g_next_id = 1;
int g_active_id = 0;        // the tab the engine is showing
bool g_next_is_spare = true;  // the next tab created is an empty one for the app to reuse (the first always is)

// One per tab (a content shell window). Every tab reports its own state, tagged with its id: "t <id> <event>".
class SailTab : public WebContentsObserver {
 public:
  explicit SailTab(WebContents* contents)
      : WebContentsObserver(contents), id_(g_next_id++) {
    Tabs().push_back(this);
  }
  ~SailTab() override {
    auto& t = Tabs();
    for (size_t i = 0; i < t.size(); ++i) {
      if (t[i] == this) {
        t.erase(t.begin() + i);
        break;
      }
    }
    Say("closed");
  }

  int id() const { return id_; }
  void Say(const std::string& what) {
    Emit(base::StringPrintf("t %d ", id_) + what);
  }
  // Empty the tab so the app can hand it to another page: blank page, then no history.
  void Recycle() {
    recycling_ = true;
    web_contents()->GetController().LoadURL(GURL("about:blank"), Referrer(),
                                            ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                            std::string());
  }

  void ReportState() {
    if (!web_contents())
      return;
    NavigationController& c = web_contents()->GetController();
    Say(base::StringPrintf("nav %d %d", c.CanGoBack() ? 1 : 0,
                           c.CanGoForward() ? 1 : 0));
    Say("url " + web_contents()->GetVisibleURL().spec());
  }

  // WebContentsObserver:
  void DidStartLoading() override { Say("loading 1"); }
  void DidStopLoading() override {
    AnnounceReady();
    Say("title " + base::UTF16ToUTF8(web_contents()->GetTitle()));   // the settled title, before "loading 0"
    Say("loading 0");
    ReportState();
  }
  void LoadProgressChanged(double progress) override {
    Say(base::StringPrintf("progress %d", static_cast<int>(progress * 100)));
  }
  void TitleWasSet(NavigationEntry* entry) override {
    if (entry)
      Say("title " + base::UTF16ToUTF8(entry->GetTitle()));
  }
  void DidStartNavigation(NavigationHandle* handle) override {
    if (!handle->IsInPrimaryMainFrame() || handle->IsSameDocument())
      return;
    Say("start " + handle->GetURL().spec());
  }
  void DidFinishNavigation(NavigationHandle* handle) override {
    if (!handle->IsInPrimaryMainFrame())
      return;
    AnnounceReady();
    if (handle->IsErrorPage()) {
      Say(base::StringPrintf("fail %d %s",
                             static_cast<int>(handle->GetNetErrorCode()),
                             handle->GetURL().spec().c_str()));
    } else if (handle->HasCommitted() && !handle->IsSameDocument()) {
      Say("commit " + handle->GetURL().spec());
      if (recycling_ && handle->GetURL().IsAboutBlank()) {
        recycling_ = false;
        NavigationController& c = web_contents()->GetController();
        if (c.CanPruneAllButLastCommitted())
          c.PruneAllButLastCommitted();
        Say("spare");
      }
    }
    ReportState();
  }
  void WebContentsDestroyed() override { delete this; }

 private:
  // The shell loads its start page (about:blank) a moment after the first tab exists, which would cancel a load
  // sent earlier. So the app is told to go ahead only once that first load has finished.
  static void AnnounceReady() {
    if (g_ready)
      return;
    g_ready = true;
    Emit("ready");
  }

  const int id_;
  bool recycling_ = false;
};

SailTab* FindTab(int id) {
  for (SailTab* t : Tabs()) {
    if (t->id() == id)
      return t;
  }
  return nullptr;
}

Shell* ShellOf(SailTab* tab) {
  if (!tab)
    return nullptr;
  for (Shell* s : Shell::windows()) {
    if (s->web_contents() == tab->web_contents())
      return s;
  }
  return nullptr;
}

Shell* ActiveShell() {
  return ShellOf(FindTab(g_active_id));
}

// The engine runs in a single process, and that mode allows one profile only: a second one (a private profile)
// trips a check in RenderProcessHostImpl. So every tab uses the one profile.
BrowserContext* Context() {
  return ShellContentBrowserClient::Get()->browser_context();
}

void Quit() {
  if (g_quitting)
    return;
  g_quitting = true;
  base::StartQnxExitWatchdog(3000);   // the process dies in 3 s even if teardown stalls
  Shell::Shutdown();
}

std::vector<std::string> Words(const std::string& arg) {
  return base::SplitString(arg, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

void HandleLine(const std::string& line) {
  const size_t sp = line.find(' ');
  const std::string cmd = line.substr(0, sp);
  const std::string arg = sp == std::string::npos ? std::string() : line.substr(sp + 1);
  if (cmd == "quit") {
    Quit();
    return;
  }
  if (cmd == "key") {
    std::vector<std::string> p = Words(arg);
    int sym = 0, mods = 0, down = 0;
    if (p.size() == 3 && base::StringToInt(p[0], &sym) &&
        base::StringToInt(p[1], &mods) && base::StringToInt(p[2], &down)) {
      ui::QnxScreenInjectKey(sym, mods, down != 0);
    }
    return;
  }
  if (cmd == "touch") {
    std::vector<std::string> p = Words(arg);
    int type = 0, x = 0, y = 0;
    if (p.size() == 3 && base::StringToInt(p[0], &type) &&
        base::StringToInt(p[1], &x) && base::StringToInt(p[2], &y)) {
      ui::QnxScreenInjectTouch(type, x, y, 9);
    }
    return;
  }
  if (cmd == "cleardata") {                   // cookies, site storage and the cache, for every site
    Context()->GetBrowsingDataRemover()->Remove(
        base::Time(), base::Time::Max(),
        BrowsingDataRemover::DATA_TYPE_COOKIES |
            BrowsingDataRemover::DATA_TYPE_DOM_STORAGE |
            BrowsingDataRemover::DATA_TYPE_CACHE,
        BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
            BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB);
    Emit("cleared");
    return;
  }
  if (cmd == "dlcancel") {
    unsigned id = 0;
    if (base::StringToUint(arg, &id))
      Downloads().Cancel(id);
    return;
  }
  if (cmd == "new") {                       // new <reserved> <url>
    const size_t s2 = arg.find(' ');
    GURL url(s2 == std::string::npos ? std::string() : arg.substr(s2 + 1));
    if (!url.is_valid())
      url = GURL("about:blank");
    Shell::CreateNewWindow(Context(), url, nullptr, gfx::Size());
    return;
  }
  if (cmd == "select" || cmd == "close") {
    int id = 0;
    if (!base::StringToInt(arg, &id))
      return;
    SailTab* tab = FindTab(id);
    Shell* shell = ShellOf(tab);
    if (!shell)
      return;
    if (cmd == "select") {
      g_active_id = id;
      SailShowShell(shell);
      return;
    }
    // close: the engine must keep one tab (closing the last window ends the process), so the last one is
    // emptied and kept as a spare.
    if (Shell::windows().size() > 1) {
      shell->Close();
    } else {
      tab->Recycle();
    }
    return;
  }
  Shell* shell = ActiveShell();
  if (!shell || !shell->web_contents())
    return;
  WebContents* wc = shell->web_contents();
  if (cmd == "load") {
    GURL url(arg);
    if (url.is_valid())
      shell->LoadURL(url);
  } else if (cmd == "back") {
    if (wc->GetController().CanGoBack())
      shell->GoBackOrForward(-1);
  } else if (cmd == "forward") {
    if (wc->GetController().CanGoForward())
      shell->GoBackOrForward(1);
  } else if (cmd == "reload") {
    shell->Reload();
  } else if (cmd == "stop") {
    shell->Stop();
  } else if (cmd == "focus") {
    wc->Focus();
  } else if (cmd == "selq") {                 // what the menu needs to know: is text selected, is a field focused
    const bool editable = wc->IsFocusedElementEditable();
    AskPage(wc, u"String(window.getSelection()).length",
            base::BindOnce([](bool editable, base::Value v) {
              Emit(base::StringPrintf("sel %d %d", v.is_int() ? v.GetInt() : 0, editable ? 1 : 0));
            }, editable));
  } else if (cmd == "copy") {                 // the selected text goes to the app, which owns the phone's clipboard
    AskPage(wc, u"String(window.getSelection())", base::BindOnce([](base::Value v) {
              if (v.is_string())
                Emit("clip " + Escape(v.GetString()));
            }));
  } else if (cmd == "paste") {                // text from the phone's clipboard, typed into the focused field
    AskPage(wc,
            u"document.execCommand('insertText', false, " +
                base::UTF8ToUTF16(base::GetQuotedJSONString(Unescape(arg))) + u")",
            base::DoNothing());
  } else if (cmd == "visible") {
    wc->UpdateWebContentsVisibility(arg == "1" ? Visibility::VISIBLE
                                               : Visibility::HIDDEN);
  }
}

void* ReaderMain(void*) {
  std::string buf;
  char tmp[2048];
  for (;;) {
    ssize_t n = read(g_fd, tmp, sizeof(tmp));
    if (n <= 0)
      break;
    buf.append(tmp, static_cast<size_t>(n));
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      if (!line.empty()) {
        GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE, base::BindOnce(&HandleLine, std::move(line)));
      }
    }
  }
  // The app is gone: never outlive it.
  GetUIThreadTaskRunner({})->PostTask(FROM_HERE, base::BindOnce(&Quit));
  sleep(4);
  _exit(0);
  return nullptr;
}

}  // namespace

bool SailEmbedded() {
  static const bool embedded = [] {
    const char* g = getenv("SAIL_GROUP");
    return g && g[0];
  }();
  return embedded;
}

void SailStartControl() {
  const char* e = getenv("SAIL_CTL_FD");
  if (!e || !e[0] || g_fd >= 0)
    return;
  g_fd = atoi(e);
  pthread_t t;
  pthread_create(&t, nullptr, ReaderMain, nullptr);
  pthread_detach(t);
  fprintf(stderr, "sail: control channel on fd %d\n", g_fd);
}

void SailAttach(WebContents* contents) {
  if (g_fd < 0 || !contents)
    return;
  for (SailTab* t : Tabs()) {
    if (t->web_contents() == contents)
      return;
  }
  Downloads().Watch(contents->GetBrowserContext()->GetDownloadManager());
  SailTab* tab = new SailTab(contents);
  g_active_id = tab->id();              // the shell shows a new tab as soon as it exists
  const bool spare = g_next_is_spare;
  g_next_is_spare = false;
  tab->Say(spare ? "spare" : "new");
}

}  // namespace content
