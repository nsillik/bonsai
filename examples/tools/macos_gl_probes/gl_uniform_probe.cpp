// gl_uniform_probe.cpp -- the second half of the macOS rendering mismatch: does a per-draw
// glUniform1i + texelFetch(samplerBuffer) loop deliver the right value to each draw?
//
// The engine does, per frame:
//   BindTextureBuffer(...)                     -- glBufferData + glTexBuffer + glUniform1i(sampler, 15)
//   GetIntegerv(GL_CURRENT_PROGRAM); GetUniformLocation(program, "DrawIndex")
//   for (i = 0; i < ~1700; ++i) { glUniform1i(DrawIndex, i); glDrawArrays(GL_TRIANGLES, First_i, Count_i); }
//
// This probe reproduces that shape with known data and checks every draw's view of both the uniform
// and the texture buffer, via transform feedback.  Two varyings are captured per vertex:
//   OutIndex = float(DrawIndex)                   -- did the uniform arrive?
//   OutTbo   = texelFetch(TransformBuffer, DrawIndex).xyz  -- did the TBO fetch follow?
//
// Texel i holds (1000+i, 2000+i, 3000+i), so any mismatch is unambiguous, and a stale/constant
// uniform is obvious (every draw reading texel 0, or texel N-1).

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#define GL_CHECK(Where)                                                        \
  do {                                                                         \
    GLenum Err = glGetError();                                                 \
    if (Err) { printf("  GL error 0x%04x at %s\n", Err, Where); }               \
  } while (0)

// Nothing in the engine uses unit 15 for anything else; this is the unit the engine picks.
static const int kTextureBufferUnit = 15;

static const int kVerticesPerDraw = 3;

struct Config
{
  const char *Name;
  uint32_t DrawsPerFrame;
  uint32_t Frames;
  bool ReuploadTboPerFrame;
  bool UseTextureBufferUnit15;
  bool BindUnrelatedTexture2D;
};

static const Config Configs[] = {
  { "A  8 draws, 1 frame, unit 0",                     8,    1, false, false, false },
  { "B  2048 draws, 1 frame, unit 0",               2048,    1, false, false, false },
  { "C  2048 draws, 30 frames, TBO reuploaded each frame, unit 15", 2048, 30, true, true, false },
  { "D  C plus an unrelated texture2D bound to unit 0", 2048, 30, true, true, true },
  { "E  1700 draws x 60 frames, engine shape",       1700,   60, true, true, true },
};

static GLuint CompileProgram()
{
  const char *Vs =
    "#version 410 core\n"
    "layout(location = 0) in vec3 in_Position;\n"
    "uniform int DrawIndex;\n"
    "uniform samplerBuffer TransformBuffer;\n"
    "out vec3 OutIndex;\n"
    "out vec3 OutTbo;\n"
    "void main()\n"
    "{\n"
    "  OutIndex = vec3(float(DrawIndex));\n"
    "  OutTbo = texelFetch(TransformBuffer, DrawIndex).xyz;\n"
    "  gl_Position = vec4(in_Position * 1e-30, 1.0);\n"
    "}\n";

  const char *Fs =
    "#version 410 core\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(1.0); }\n";

  GLuint Program = glCreateProgram();

  GLuint VsId = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(VsId, 1, &Vs, 0);
  glCompileShader(VsId);
  GLint Ok = 0;
  glGetShaderiv(VsId, GL_COMPILE_STATUS, &Ok);
  if (!Ok) { char Log[8192] = {}; glGetShaderInfoLog(VsId, 8191, 0, Log); printf("VS FAIL:\n%s\n", Log); exit(1); }
  glAttachShader(Program, VsId);

  GLuint FsId = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(FsId, 1, &Fs, 0);
  glCompileShader(FsId);
  glAttachShader(Program, FsId);

  const char *Varyings[] = { "OutIndex", "OutTbo" };
  glTransformFeedbackVaryings(Program, 2, Varyings, GL_INTERLEAVED_ATTRIBS);

  glLinkProgram(Program);
  glGetProgramiv(Program, GL_LINK_STATUS, &Ok);
  if (!Ok) { char Log[8192] = {}; glGetProgramInfoLog(Program, 8191, 0, Log); printf("LINK FAIL:\n%s\n", Log); exit(1); }

  return Program;
}

int main()
{
  CGLPixelFormatAttribute Attributes[] = {
    kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
    kCGLPFAAccelerated,
    (CGLPixelFormatAttribute)0
  };
  CGLPixelFormatObj PixelFormat = 0;
  GLint NumFormats = 0;
  if (CGLChoosePixelFormat(Attributes, &PixelFormat, &NumFormats) || !PixelFormat) { printf("FATAL: pixel format\n"); return 1; }
  CGLContextObj Context = 0;
  if (CGLCreateContext(PixelFormat, 0, &Context) || !Context) { printf("FATAL: context\n"); return 1; }
  CGLSetCurrentContext(Context);

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
  printf("GL_RENDERER : %s\n\n", glGetString(GL_RENDERER));

  GLuint Program = CompileProgram();
  GLint DrawIndexUniform = glGetUniformLocation(Program, "DrawIndex");
  GLint TransformBufferSampler = glGetUniformLocation(Program, "TransformBuffer");
  printf("DrawIndex uniform location = %d, TransformBuffer sampler location = %d\n\n",
         DrawIndexUniform, TransformBufferSampler);
  if (DrawIndexUniform < 0 || TransformBufferSampler < 0) { printf("FATAL: uniform missing\n"); return 1; }

  // Vertex buffer: all zeros; the shader keeps it live but the interesting data is the TBO.
  GLuint Vao = 0, Vbo = 0;
  glGenVertexArrays(1, &Vao);
  glBindVertexArray(Vao);
  glGenBuffers(1, &Vbo);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  std::vector<float> Zeros(64 * 3, 0.0f);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(Zeros.size() * sizeof(float)), Zeros.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(0);

  // Texture buffer holding N texels of (1000+i, 2000+i, 3000+i).
  const uint32_t MaxDraws = 2048;
  std::vector<float> Texels(MaxDraws * 4);
  for (uint32_t i = 0; i < MaxDraws; ++i)
  {
    Texels[i * 4 + 0] = 1000.0f + (float)i;
    Texels[i * 4 + 1] = 2000.0f + (float)i;
    Texels[i * 4 + 2] = 3000.0f + (float)i;
    Texels[i * 4 + 3] = 0.0f;
  }

  GLuint TboBuffer = 0, TboTexture = 0;
  glGenBuffers(1, &TboBuffer);
  glGenTextures(1, &TboTexture);

  // Framebuffer, required or glDrawArrays is rejected with GL_INVALID_FRAMEBUFFER_OPERATION.
  GLuint ColorTexture = 0;
  glGenTextures(1, &ColorTexture);
  glBindTexture(GL_TEXTURE_2D, ColorTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  GLuint Fbo = 0;
  glGenFramebuffers(1, &Fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ColorTexture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("FATAL: fbo\n"); return 1; }

  // TF target: one frame's worth of draws, two varyings (6 floats) per vertex.
  const size_t MaxCaptureFloats = (size_t)MaxDraws * kVerticesPerDraw * 6;
  GLuint FeedbackBuffer = 0;
  glGenBuffers(1, &FeedbackBuffer);
  glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, FeedbackBuffer);
  glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (GLsizeiptr)(MaxCaptureFloats * sizeof(float)), 0, GL_DYNAMIC_READ);

  glEnable(GL_RASTERIZER_DISCARD);
  glUseProgram(Program);

  uint32_t TotalChecks = 0, TotalMismatch = 0;

  for (size_t ConfigIndex = 0; ConfigIndex < sizeof(Configs) / sizeof(Configs[0]); ++ConfigIndex)
  {
    const Config &C = Configs[ConfigIndex];
    printf("=== %s ===\n", C.Name);

    uint32_t IndexMismatches = 0, TboMismatches = 0;
    uint32_t FirstIndexMismatchDraw = 0xffffffffu, FirstTboMismatchDraw = 0xffffffffu;
    float FirstBadIndex = 0, FirstBadTbo = 0, FirstWantTbo = 0;
    uint32_t FramesChecked = 0;

    for (uint32_t Frame = 0; Frame < C.Frames; ++Frame)
    {
      if (C.ReuploadTboPerFrame || Frame == 0)
      {
        glBindBuffer(GL_TEXTURE_BUFFER, TboBuffer);
        glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(Texels.size() * sizeof(float)), Texels.data(), GL_DYNAMIC_DRAW);
      }

      int Unit = C.UseTextureBufferUnit15 ? kTextureBufferUnit : 0;
      glActiveTexture(GL_TEXTURE0 + Unit);
      glBindTexture(GL_TEXTURE_BUFFER, TboTexture);
      glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, TboBuffer);
      glUniform1i(TransformBufferSampler, Unit);
      GL_CHECK("tbo bind");

      if (C.BindUnrelatedTexture2D)
      {
        // The engine has other samplers on other units; make sure that does not disturb unit 15.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ColorTexture);
        glUniform1i(glGetUniformLocation(Program, "TransformBuffer"), Unit);
        glActiveTexture(GL_TEXTURE0 + Unit);
      }

      glBindVertexArray(Vao);
      glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, FeedbackBuffer);

      glBeginTransformFeedback(GL_TRIANGLES);
      for (uint32_t Draw = 0; Draw < C.DrawsPerFrame; ++Draw)
      {
        glUniform1i(DrawIndexUniform, (GLint)Draw);
        glDrawArrays(GL_TRIANGLES, 0, kVerticesPerDraw);
      }
      glEndTransformFeedback();
      GL_CHECK("draw loop");

      GLsync Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      glClientWaitSync(Fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
      glDeleteSync(Fence);

      size_t FloatCount = (size_t)C.DrawsPerFrame * kVerticesPerDraw * 6;
      float *Captured = (float *)glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                                                 (GLsizeiptr)(FloatCount * sizeof(float)), GL_MAP_READ_BIT);
      if (!Captured) { printf("  frame %u: MAP FAILED\n", Frame); continue; }

      for (uint32_t Draw = 0; Draw < C.DrawsPerFrame; ++Draw)
      {
        // Check the first vertex of each draw; every vertex of a draw must agree.
        const float *Got = Captured + ((size_t)Draw * kVerticesPerDraw) * 6;
        float WantIndex = (float)Draw;
        float WantTbo = 1000.0f + (float)Draw;

        TotalChecks++;
        if (Got[0] != WantIndex)
        {
          if (!IndexMismatches) { FirstIndexMismatchDraw = Draw; FirstBadIndex = Got[0]; }
          IndexMismatches++;
        }
        if (Got[3] != WantTbo)
        {
          if (!TboMismatches) { FirstTboMismatchDraw = Draw; FirstBadTbo = Got[3]; FirstWantTbo = WantTbo; }
          TboMismatches++;
        }
      }

      FramesChecked++;
      glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
    }

    printf("  frames checked: %u\n", FramesChecked);
    printf("  uniform DrawIndex mismatches: %u", IndexMismatches);
    if (IndexMismatches) { printf("  (first: draw %u got %.0f)\n", FirstIndexMismatchDraw, FirstBadIndex); } else { printf("\n"); }
    printf("  TBO fetch mismatches:         %u", TboMismatches);
    if (TboMismatches) { printf("  (first: draw %u got %.0f wanted %.0f)\n", FirstTboMismatchDraw, FirstBadTbo, FirstWantTbo); } else { printf("\n"); }
    printf("\n");

    TotalMismatch += IndexMismatches + TboMismatches;
  }

  printf("=== %u mismatches across %u (config, draw) checks ===\n", TotalMismatch, TotalChecks);
  return 0;
}
