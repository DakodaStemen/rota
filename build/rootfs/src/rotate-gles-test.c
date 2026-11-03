// SPDX-License-Identifier: MIT
// rotate-gles-test [-t SECONDS] [-d /dev/dri/cardN] [-o]   (-o: offscreen only, no modeset)
// Minimal GLES2 smoke test for the first-boot self-test (kmscube is not packaged for Arch ARM).
// Mode 1 (default): KMS + GBM + EGL, fullscreen spinning gradient quad at the connector's preferred mode,
//   page-flipped on vblank, then restores the previous CRTC. Mode 2 (no connected connector, or -o):
//   EGL surfaceless + FBO, same shader, no display. Prints GL strings and "RESULT mode=... frames=N secs=S fps=F".
// Exit: 0 rendered >= 1 frame, 1 usage, 2 no DRM device, 3 EGL/GLES setup failed, 4 no frame rendered.
// Build: gcc -O2 -o rotate-gles-test rotate-gles-test.c -lEGL -lGLESv2 -lgbm -ldrm $(pkg-config --cflags libdrm)
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

/* a is mediump in BOTH stages: GLSL ES 1.00 requires a uniform shared by two stages to have the same precision
 * (the vertex default is highp, so a bare "uniform float a" here failed to link). */
static const char *VS = "attribute vec2 p; varying vec2 uv; uniform mediump float a;"
	"void main(){ float c=cos(a), s=sin(a); uv=p; gl_Position=vec4(mat2(c,-s,s,c)*p*0.9,0.,1.); }";
static const char *FS = "precision mediump float; varying vec2 uv; uniform float a;"
	"void main(){ float r=0.; for(int i=0;i<24;i++) r+=sin(uv.x*float(i)+a)*cos(uv.y*float(i)-a);"
	" gl_FragColor=vec4(0.5+0.5*uv.x, 0.5+0.5*uv.y, 0.5+0.02*r, 1.); }";

static GLuint sh(GLenum t, const char *src)
{
	GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, 0); glCompileShader(s);
	GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char l[512]; glGetShaderInfoLog(s, sizeof l, 0, l); fprintf(stderr, "shader: %s\n", l); return 0; }
	return s;
}
static GLint ua;
static int gl_init(void)
{
	GLuint v = sh(GL_VERTEX_SHADER, VS), f = sh(GL_FRAGMENT_SHADER, FS);
	if (!v || !f) return -1;
	GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
	glBindAttribLocation(p, 0, "p"); glLinkProgram(p);
	GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); if (!ok) { char l[512]; glGetProgramInfoLog(p, sizeof l, 0, l); fprintf(stderr, "link failed: %s\n", l); return -1; }
	glUseProgram(p); ua = glGetUniformLocation(p, "a");
	static const GLfloat q[] = { -1,-1, 1,-1, -1,1, 1,1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, q); glEnableVertexAttribArray(0);
	printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
	return 0;
}
static void draw(double t) { glUniform1f(ua, (float)t); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); }

static EGLDisplay get_dpy(EGLenum plat, void *native)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	return gpd ? gpd(plat, native, NULL) : eglGetDisplay((EGLNativeDisplayType)native);
}

static int fb_flip_pending;
static void on_flip(int fd, unsigned f, unsigned s, unsigned us, void *d) { fb_flip_pending = 0; }

static int run_kms(const char *dev, double secs, int *frames)
{
	int fd = open(dev, O_RDWR | O_CLOEXEC); if (fd < 0) { perror(dev); return 2; }
	drmModeRes *res = drmModeGetResources(fd); if (!res) { fprintf(stderr, "%s: no KMS resources\n", dev); close(fd); return 5; }
	drmModeConnector *c = NULL;
	for (int i = 0; i < res->count_connectors && !c; i++) {
		drmModeConnector *x = drmModeGetConnector(fd, res->connectors[i]);
		if (x && x->connection == DRM_MODE_CONNECTED && x->count_modes) c = x; else if (x) drmModeFreeConnector(x);
	}
	if (!c) { fprintf(stderr, "no connected connector with modes\n"); drmModeFreeResources(res); close(fd); return 5; }
	drmModeModeInfo mode = c->modes[0];
	for (int i = 0; i < c->count_modes; i++) if (c->modes[i].type & DRM_MODE_TYPE_PREFERRED) { mode = c->modes[i]; break; }
	drmModeEncoder *e = c->encoder_id ? drmModeGetEncoder(fd, c->encoder_id) : NULL;
	uint32_t crtc_id = e ? e->crtc_id : 0;
	for (int i = 0; !crtc_id && i < res->count_crtcs; i++) {
		for (int k = 0; k < c->count_encoders && !crtc_id; k++) {
			drmModeEncoder *en = drmModeGetEncoder(fd, c->encoders[k]);
			if (en && (en->possible_crtcs & (1u << i))) crtc_id = res->crtcs[i];
			if (en) drmModeFreeEncoder(en);
		}
	}
	if (!crtc_id) { fprintf(stderr, "no CRTC\n"); return 5; }
	drmModeCrtc *old = drmModeGetCrtc(fd, crtc_id);
	printf("KMS %s connector %u mode %s %ux%u@%u crtc %u\n", dev, c->connector_id, mode.name, mode.hdisplay, mode.vdisplay, mode.vrefresh, crtc_id);

	struct gbm_device *gbm = gbm_create_device(fd); if (!gbm) { fprintf(stderr, "gbm_create_device failed\n"); return 3; }
	struct gbm_surface *gs = gbm_surface_create(gbm, mode.hdisplay, mode.vdisplay, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!gs) { fprintf(stderr, "gbm_surface_create failed\n"); return 3; }
	EGLDisplay d = get_dpy(EGL_PLATFORM_GBM_KHR, gbm); EGLint maj, min;
	if (d == EGL_NO_DISPLAY || !eglInitialize(d, &maj, &min)) { fprintf(stderr, "eglInitialize failed 0x%x\n", eglGetError()); return 3; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
	EGLConfig cfg = 0; EGLint n = 0; EGLConfig all[64]; eglChooseConfig(d, ca, all, 64, &n);
	for (int i = 0; i < n; i++) { EGLint id; eglGetConfigAttrib(d, all[i], EGL_NATIVE_VISUAL_ID, &id); if (id == GBM_FORMAT_XRGB8888) { cfg = all[i]; break; } }
	if (!cfg) { fprintf(stderr, "no EGL config for XRGB8888 (%d candidates)\n", n); return 3; }
	EGLint cx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
	EGLSurface es = eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)gs, NULL);
	if (ctx == EGL_NO_CONTEXT || es == EGL_NO_SURFACE || !eglMakeCurrent(d, es, es, ctx)) { fprintf(stderr, "EGL context/surface failed 0x%x\n", eglGetError()); return 3; }
	printf("EGL %d.%d vendor=%s\n", maj, min, eglQueryString(d, EGL_VENDOR));
	if (gl_init()) return 3;
	glViewport(0, 0, mode.hdisplay, mode.vdisplay);

	struct gbm_bo *prev = NULL; uint32_t prev_fb = 0; double t0 = now(); int first = 1;
	drmEventContext ev = { .version = 2, .page_flip_handler = on_flip };
	while (now() - t0 < secs) {
		draw(now() - t0); eglSwapBuffers(d, es);
		struct gbm_bo *bo = gbm_surface_lock_front_buffer(gs); uint32_t fb;
		if (drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, &fb)) { perror("AddFB"); break; }
		if (first) { if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &c->connector_id, 1, &mode)) { perror("SetCrtc"); break; } first = 0; }
		else {
			fb_flip_pending = 1;
			if (drmModePageFlip(fd, crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT, NULL)) { perror("PageFlip"); break; }
			struct pollfd p = { fd, POLLIN, 0 };
			while (fb_flip_pending) { if (poll(&p, 1, 1000) <= 0) { fprintf(stderr, "flip timeout\n"); fb_flip_pending = 0; goto done; } drmHandleEvent(fd, &ev); }
		}
		if (prev) { drmModeRmFB(fd, prev_fb); gbm_surface_release_buffer(gs, prev); }
		prev = bo; prev_fb = fb; (*frames)++;
	}
done:
	if (old) drmModeSetCrtc(fd, old->crtc_id, old->buffer_id, old->x, old->y, &c->connector_id, 1, &old->mode);
	return 0;
}

static int run_offscreen(double secs, int *frames)
{
	EGLDisplay d = get_dpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY); EGLint maj, min;
	if (d == EGL_NO_DISPLAY || !eglInitialize(d, &maj, &min)) { fprintf(stderr, "surfaceless eglInitialize failed 0x%x\n", eglGetError()); return 3; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLConfig cfg; EGLint n = 0;
	if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) { fprintf(stderr, "no pbuffer config\n"); return 3; }
	EGLint pa[] = { EGL_WIDTH, 720, EGL_HEIGHT, 720, EGL_NONE }, cx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLSurface s = eglCreatePbufferSurface(d, cfg, pa); EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
	if (s == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, ctx)) { fprintf(stderr, "offscreen context failed 0x%x\n", eglGetError()); return 3; }
	printf("EGL %d.%d vendor=%s (surfaceless)\n", maj, min, eglQueryString(d, EGL_VENDOR));
	if (gl_init()) return 3;
	glViewport(0, 0, 720, 720);
	double t0 = now();
	while (now() - t0 < secs) { draw(now() - t0); glFinish(); (*frames)++; }
	return 0;
}

int main(int argc, char **argv)
{
	double secs = 5; const char *dev = NULL; int off = 0, o;
	while ((o = getopt(argc, argv, "t:d:oh")) != -1) {
		if (o == 't') secs = atof(optarg); else if (o == 'd') dev = optarg; else if (o == 'o') off = 1;
		else { fprintf(stderr, "usage: %s [-t SECONDS] [-d /dev/dri/cardN] [-o]\n", argv[0]); return 1; }
	}
	int frames = 0, rc = 2; const char *mode = "kms"; double t0 = now();
	if (!off) {
		if (dev) rc = run_kms(dev, secs, &frames);
		else for (int i = 0; i < 4 && rc != 0; i++) { char p[32]; snprintf(p, sizeof p, "/dev/dri/card%d", i); if (access(p, R_OK | W_OK) == 0) { frames = 0; rc = run_kms(p, secs, &frames); if (rc == 0 && !frames) rc = 5; } }
	}
	if (rc != 0) {
		if (!off) fprintf(stderr, "KMS path unavailable (rc=%d), falling back to offscreen\n", rc);
		mode = "offscreen"; frames = 0; t0 = now(); rc = run_offscreen(secs, &frames);
	}
	double el = now() - t0;
	printf("RESULT mode=%s frames=%d secs=%.2f fps=%.1f\n", mode, frames, el, frames / (el > 0 ? el : 1));
	return rc ? rc : (frames ? 0 : 4);
}
