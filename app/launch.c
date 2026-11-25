/* Sail engine launcher: started by the Sail app (src/engine.cpp), sets up the environment the engine needs and
 * execs it. Our own code (the port's launcher also uploads logs; this one sends nothing).
 * From the app, in the environment: SAIL_GROUP, SAIL_WINDOW_ID, SAIL_CTL_FD (window group and control socket),
 * SAIL_OUT_W/H (page area on the panel), SAIL_RENDER_W/H (pixels really drawn), SAIL_DPR (device px per CSS px).
 * Test switches, files in the app's data dir ($HOME): "nogpu" (software rendering), "args" (extra engine
 * arguments, one per line). Engine output goes to $HOME/sail.log. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char lines[32][512];

int main(int argc, char **argv) {
  static char dir[1024], work[1024], buf[2048], exe[1100], url[1024] = "about:blank", udd[1100], scale[64];
  int fd = open("/proc/self/exefile", O_RDONLY), n = 0, i, nl = 0;
  ssize_t r = fd >= 0 ? read(fd, dir, sizeof(dir) - 1) : 0;
  if (fd >= 0) close(fd);
  if (r <= 0) return 1;
  dir[r] = 0;
  while (r > 0 && (dir[r - 1] == '\n' || dir[r - 1] == ' ')) dir[--r] = 0;
  *strrchr(dir, '/') = 0;                                  /* <sandbox>/app/native */

  const char *home = getenv("HOME");                       /* the sandbox's writable data dir */
  if (home && access(home, W_OK) == 0) snprintf(work, sizeof(work), "%s", home);
  else { snprintf(work, sizeof(work), "%s", dir); char *p = strstr(work, "/app/native"); if (p) strcpy(p, "/data"); }
  chdir(work);
  setenv("HOME", work, 1);

  fd = open("sail.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
  fprintf(stderr, "sail-launch: dir=%s work=%s\n", dir, work);

  snprintf(buf, sizeof(buf), "%s:%s", dir, getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "");
  setenv("LD_LIBRARY_PATH", buf, 1);
  snprintf(buf, sizeof(buf), "%s/root_store.certs", dir);
  setenv("QNX_CA_BUNDLE", buf, 1);
  /* The page area and how many pixels of it are drawn; the window is scaled to the area by Screen. */
  const char *ow = getenv("SAIL_OUT_W"), *oh = getenv("SAIL_OUT_H"), *rw = getenv("SAIL_RENDER_W"), *rh = getenv("SAIL_RENDER_H");
  const char *dpr = getenv("SAIL_DPR");
  setenv("QNX_SCREEN_WIDTH", rw ? rw : "1080", 1); setenv("QNX_SCREEN_HEIGHT", rh ? rh : "975", 1);
  setenv("QNX_SCREEN_OUTPUT_WIDTH", ow ? ow : "1440", 1); setenv("QNX_SCREEN_OUTPUT_HEIGHT", oh ? oh : "1300", 1);
  setenv("QNX_SCREEN_ROTATION", "0", 1);                   /* same as the app's own window */
  setenv("QNX_INPUT_POLL_MS", "1", 0); setenv("QNX_MAX_FPS", "45", 0);

  FILE *f;
  int gpu = access("nogpu", F_OK) != 0;
  fprintf(stderr, "sail-launch: url=%s gpu=%d\n", url, gpu);

  snprintf(exe, sizeof(exe), "%s/sail-engine", dir);
  snprintf(udd, sizeof(udd), "--user-data-dir=%s/profile", work);
  char *a[96];
  a[n++] = exe;
  a[n++] = "--no-sandbox"; a[n++] = "--no-zygote"; a[n++] = "--single-process";
  snprintf(scale, sizeof(scale), "--force-device-scale-factor=%s", dpr ? dpr : "2.25");
  a[n++] = "--ozone-platform=qnx_screen"; a[n++] = scale;
  if (gpu) { a[n++] = "--use-gl=egl"; a[n++] = "--ignore-gpu-blocklist"; a[n++] = "--enable-gpu-rasterization"; }
  else { a[n++] = "--disable-gpu"; a[n++] = "--disable-gpu-compositing"; }
  /* 2D canvas is drawn in software and composited by the GPU: on this phone the GPU canvas was no faster at drawing
   * (52 against 45 runs/s) and far slower whenever a page read pixels back (0.1 against 32 runs/s). */
  a[n++] = "--disable-accelerated-2d-canvas";
  a[n++] = "--num-raster-threads=4";
  a[n++] = "--disable-accelerated-video-decode";
  a[n++] = "--disable-features=NetworkServiceDedicatedThread,MojoIpcz,Translate,OptimizationHints,MediaRouter,"
           "BackForwardCache,AutofillServerCommunication,FedCm,PrivacySandboxAdsAPIs,SharedStorageAPI,PrivateAggregationApi";
  a[n++] = "--disable-background-networking"; a[n++] = "--disable-sync"; a[n++] = "--disable-component-update";
  a[n++] = "--disable-domain-reliability"; a[n++] = "--disable-client-side-phishing-detection";
  a[n++] = "--no-first-run"; a[n++] = "--disable-renderer-accessibility";
  a[n++] = "--enable-low-end-device-mode"; a[n++] = "--use-mobile-user-agent";
  a[n++] = "--enable-logging=stderr";
  a[n++] = udd;
  f = fopen("args", "r");
  if (f) { while (nl < 32 && fgets(lines[nl], sizeof(lines[nl]), f)) { lines[nl][strcspn(lines[nl], "\r\n")] = 0; if (lines[nl][0]) { a[n++] = lines[nl]; nl++; } } fclose(f); }
  a[n++] = url; a[n] = 0;
  for (i = 0; i < n; i++) fprintf(stderr, "sail-launch: arg %s\n", a[i]);
  fflush(stderr);
  execv(exe, a);
  perror("sail-launch: execv");
  return 1;
}
