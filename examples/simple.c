#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/os.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __SWITCH__
static FILE* switch_log_file(void) {
  static FILE* file = NULL;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    file = fopen("sdmc:/aurora_simple.log", "w");
  }
  return file;
}
#endif

static unsigned char gx_fifo[256 * 1024] __attribute__((aligned(32)));
static unsigned int frame_counter = 0;
static GXTexObj checker_texture;
static unsigned char checker_pixels[64 * 64 * 4] __attribute__((aligned(32)));

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
  const char* levelStr;
  FILE* out = stdout;
  switch (level) {
  case LOG_DEBUG:
    levelStr = "DEBUG";
    break;
  case LOG_INFO:
    levelStr = "INFO";
    break;
  case LOG_WARNING:
    levelStr = "WARNING";
    break;
  case LOG_ERROR:
    levelStr = "ERROR";
    out = stderr;
    break;
  case LOG_FATAL:
    levelStr = "FATAL";
    out = stderr;
    break;
  }
#ifdef __SWITCH__
  FILE* file = switch_log_file();
  if (file != NULL) {
    out = file;
  }
#endif
  fprintf(out, "[%s: %s;%s]\n", levelStr, module, message);
  fflush(out);
  if (level == LOG_FATAL) {
    abort();
  }
}

static void init_gx() {
  GXInit(gx_fifo, sizeof(gx_fifo));

  const float width = 640.0f;
  const float height = 480.0f;
  float projection[4][4] = {
      {2.0f / width, 0.0f, 0.0f, -1.0f},
      {0.0f, -2.0f / height, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  };

  GXSetProjection(projection, GX_ORTHOGRAPHIC);
  GXSetViewport(0.0f, 0.0f, width, height, 0.0f, 1.0f);
  GXSetScissor(0, 0, (unsigned int)width, (unsigned int)height);

  GXSetCullMode(GX_CULL_NONE);
  GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);

  for (unsigned int y = 0; y < 64; ++y) {
    for (unsigned int x = 0; x < 64; ++x) {
      const unsigned int offset = (y * 64 + x) * 4;
      const bool checker = (((x >> 3) ^ (y >> 3)) & 1) != 0;
      checker_pixels[offset + 0] = checker ? 255 : (unsigned char)(x * 4);
      checker_pixels[offset + 1] = checker ? (unsigned char)(y * 4) : 255;
      checker_pixels[offset + 2] = checker ? 40 : 220;
      checker_pixels[offset + 3] = 255;
    }
  }

  GXInitTexObj(&checker_texture, checker_pixels, 64, 64, GX_TF_RGBA8_PC, GX_REPEAT, GX_REPEAT, GX_FALSE);
  GXInitTexObjLOD(&checker_texture, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

static void configure_color_triangle() {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetNumTexGens(0);
  GXSetNumTevStages(1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void configure_textured_quad() {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

  GXSetNumChans(0);
  GXSetNumTexGens(1);
  GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GXSetNumTevStages(1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
  GXLoadTexObj(&checker_texture, GX_TEXMAP0);
}

static void draw_triangle() {
  configure_color_triangle();
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  GXPosition3f32(70.0f, 370.0f, 0.0f);
  GXColor4u8(255, 40, 40, 255);
  GXPosition3f32(290.0f, 370.0f, 0.0f);
  GXColor4u8(40, 255, 80, 255);
  GXPosition3f32(180.0f, 120.0f, 0.0f);
  GXColor4u8(60, 150, 255, 255);
  GXEnd();
}

static void draw_textured_quad() {
  configure_textured_quad();
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 6);
  GXPosition3f32(360.0f, 130.0f, 0.0f);
  GXTexCoord2f32(0.0f, 0.0f);
  GXPosition3f32(580.0f, 130.0f, 0.0f);
  GXTexCoord2f32(1.0f, 0.0f);
  GXPosition3f32(580.0f, 370.0f, 0.0f);
  GXTexCoord2f32(1.0f, 1.0f);

  GXPosition3f32(360.0f, 130.0f, 0.0f);
  GXTexCoord2f32(0.0f, 0.0f);
  GXPosition3f32(580.0f, 370.0f, 0.0f);
  GXTexCoord2f32(1.0f, 1.0f);
  GXPosition3f32(360.0f, 370.0f, 0.0f);
  GXTexCoord2f32(0.0f, 1.0f);
  GXEnd();
}

static void draw() {
  ++frame_counter;
  GXSetCopyClear(
      (GXColor){
          .r = 0,
          .g = (unsigned char)((frame_counter >> 2) & 0x3f),
          .b = 100,
          .a = 255,
      },
      GX_MAX_Z24);
  draw_triangle();
  draw_textured_quad();
}

int main(int argc, char* argv[]) {
  const AuroraConfig config = {
      .appName = "Demo",
      .logCallback = &log_callback,
  };
  AuroraInfo initInfo = aurora_initialize(argc, argv, &config);
  init_gx();

  bool exiting = false;
  bool paused = false;
  while (!exiting) {
    const AuroraEvent* event = aurora_update();
    while (event != NULL && event->type != AURORA_NONE) {
      switch (event->type) {
      case AURORA_EXIT:
        exiting = true;
        break;
      case AURORA_PAUSED:
        paused = true;
        break;
      case AURORA_UNPAUSED:
        paused = false;
        break;
      case AURORA_WINDOW_RESIZED:
        initInfo.windowSize = event->windowSize;
        break;
      default:
        break;
      }
      ++event;
    }
    if (exiting || paused || !aurora_begin_frame()) {
      continue;
    }
    draw();
    aurora_end_frame();
  }

  aurora_shutdown();
  return 0;
}
