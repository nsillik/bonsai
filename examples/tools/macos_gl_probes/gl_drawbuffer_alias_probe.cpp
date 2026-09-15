// gl_drawbuffer_alias_probe.cpp -- minimal repro for the near-empty terrain_gen world.
//
// Question: on this driver, does a draw into a framebuffer whose two draw buffers alias the *same*
// image write anything?
//
// Background: `FramebufferTexture` (framebuffer.cpp) attaches to the next free attachment slot and
// increments `FBO->Attachments`.  `render_init.cpp`'s Terrain Decoration block called it a second
// time on a framebuffer that `InitializeRenderToTextureFramebuffer` had already attached, so one
// texture ended up on COLOR_ATTACHMENT0 *and* COLOR_ATTACHMENT1 and `SetDrawBuffers` enabled both.
// The decoration pass then wrote ~0.02% of its target with no GL error, which starved the voxel
// finalize pass and left terrain_gen with sparse, streaky terrain.  Mesa tolerates the alias, so
// this only ever showed up on macOS.
//
// Method: four framebuffer configurations, each cleared to a sentinel and then drawn into with the
// engine's own quad -- the same six vertices, GL_FLOAT x3 stride 0, and the same passthrough vertex
// shader with gl_Position.z = 1.0.  Count the texels that still hold the sentinel afterwards, and
// report the framebuffer's completeness status for each.  No window and no display: an offscreen
// CGL core-profile context, a 66 x 4356 RGBA32F target (the engine's TextureDim) and a readback.
//
// Build (both arches, to separate an Apple driver behaviour from a Rosetta GL-client one):
//   clang++ -std=c++17 -O1 -o glprobe_alias_x86 -target x86_64-apple-macos11 gl_drawbuffer_alias_probe.cpp -framework OpenGL
//   clang++ -std=c++17 -O1 -o glprobe_alias_arm gl_drawbuffer_alias_probe.cpp -framework OpenGL

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// The engine's terrain noise target: 66 wide, 66*66 tall (render_init.cpp: `TextureDim`).
static const int kWidth  = 66;
static const int kHeight = 66 * 66;

// What the fragment shader writes into alpha.  The clear value must be unreachable by the shader so
// "still holds the clear value" means "no fragment was written here".
static const float kShaderAlpha = 0.25f;
static const float kSentinel    = -1234.5f;

static const float kQuadVertices[18] = {
  -1.0f, -1.0f, 1.0f,
   1.0f, -1.0f, 1.0f,
  -1.0f,  1.0f, 1.0f,
  -1.0f,  1.0f, 1.0f,
   1.0f, -1.0f, 1.0f,
   1.0f,  1.0f, 1.0f,
};

static const char *VertexSource =
  "#version 410 core\n"
  "layout(location = 0) in vec3 vertexPosition_modelspace;\n"
  "void main()\n"
  "{\n"
  "  gl_Position = vec4(vertexPosition_modelspace, 1);\n"   // z = 1.0, as the engine's Passthrough
  "}\n";

static const char *FragmentSource =
  "#version 410 core\n"
  "out vec4 FragColor;\n"
  "void main() { FragColor = vec4(0.5, 0.5, 0.5, 0.25); }\n";

#define GL_CHECK(Where)                                                        \
  do {                                                                         \
    GLenum Err = glGetError();                                                 \
    if (Err) { printf("  GL error 0x%04x at %s\n", Err, Where); }               \
  } while (0)

static GLuint CompileProgram()
{
  GLuint Vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(Vs, 1, &VertexSource, 0);
  glCompileShader(Vs);

  GLuint Fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(Fs, 1, &FragmentSource, 0);
  glCompileShader(Fs);

  GLuint Program = glCreateProgram();
  glAttachShader(Program, Vs);
  glAttachShader(Program, Fs);
  glLinkProgram(Program);

  GLint Ok = 0;
  glGetProgramiv(Program, GL_LINK_STATUS, &Ok);
  if (!Ok)
  {
    char Log[8192] = {};
    glGetProgramInfoLog(Program, sizeof(Log) - 1, 0, Log);
    printf("FATAL: program failed to link:\n%s\n", Log);
    exit(1);
  }

  return Program;
}

static GLuint MakeTargetTexture(const char *Name)
{
  GLuint Texture = 0;
  glGenTextures(1, &Texture);
  glBindTexture(GL_TEXTURE_2D, Texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kWidth, kHeight, 0, GL_RGBA, GL_FLOAT, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GL_CHECK(Name);
  return Texture;
}

struct Variant
{
  const char *Name;
  bool AliasSameTextureOnA1;
  bool SecondTextureOnA1;
  int  DrawBufferCount;
};

static const Variant Variants[] = {
  { "v0 one attachment, 1 draw buffer (control)",           false, false, 1 },
  { "v1 same texture on A0 and A1, 2 draw buffers (engine)", true,  false, 2 },
  { "v2 same texture on A0 and A1, 1 draw buffer (fix)",     true,  false, 1 },
  { "v3 different textures on A0 and A1, 2 draw buffers",    false, true,  2 },
};

int main()
{
  CGLPixelFormatAttribute Attributes[] = {
    kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
    kCGLPFAAccelerated,
    (CGLPixelFormatAttribute)0
  };
  CGLPixelFormatObj PixelFormat = 0;
  GLint NumFormats = 0;
  CGLError Err = CGLChoosePixelFormat(Attributes, &PixelFormat, &NumFormats);
  if (Err || !PixelFormat) { printf("FATAL: CGLChoosePixelFormat failed (%d)\n", Err); return 1; }

  CGLContextObj Context = 0;
  Err = CGLCreateContext(PixelFormat, 0, &Context);
  if (Err || !Context) { printf("FATAL: CGLCreateContext failed (%d)\n", Err); return 1; }
  CGLSetCurrentContext(Context);

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
  printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
  printf("target      : %dx%d RGBA32F, quad at NDC z=1.0, shader writes alpha %.2f\n\n",
         kWidth, kHeight, kShaderAlpha);

  GLuint Program = CompileProgram();

  GLuint QuadBuffer = 0;
  glGenBuffers(1, &QuadBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, QuadBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

  GLuint QuadVAO = 0;
  glGenVertexArrays(1, &QuadVAO);
  glBindVertexArray(QuadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, QuadBuffer);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(0);
  GL_CHECK("quad setup");

  GLuint TargetA = MakeTargetTexture("target A");
  GLuint TargetB = MakeTargetTexture("target B");

  GLuint Fbo = 0;
  glGenFramebuffers(1, &Fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);

  std::vector<float> Pixels((size_t)kWidth * kHeight * 4);

  for (size_t VariantIndex = 0; VariantIndex < sizeof(Variants) / sizeof(Variants[0]); ++VariantIndex)
  {
    const Variant &V = Variants[VariantIndex];
    printf("=== %s ===\n", V.Name);

    // Every variant starts from a clean attachment set; the A1 attachment is what differs.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, TargetA, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
    if (V.AliasSameTextureOnA1) { glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, TargetA, 0); }
    if (V.SecondTextureOnA1)    { glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, TargetB, 0); }

    GLenum DrawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(V.DrawBufferCount, DrawBuffers);
    GL_CHECK("draw buffers");

    GLenum Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("  framebuffer status 0x%04x (%s)\n", Status,
           Status == GL_FRAMEBUFFER_COMPLETE ? "complete" : "INCOMPLETE");

    glViewport(0, 0, kWidth, kHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(kSentinel, kSentinel, kSentinel, kSentinel);
    glClear(GL_COLOR_BUFFER_BIT);
    GL_CHECK("clear");

    glUseProgram(Program);
    glBindVertexArray(QuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    GLenum DrawErr = glGetError();

    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_FLOAT, Pixels.data());
    GLenum ReadErr = glGetError();

    size_t Total = (size_t)kWidth * kHeight;
    size_t Unwritten = 0;
    size_t Written = 0;
    for (size_t i = 0; i < Total; ++i)
    {
      float Alpha = Pixels[i * 4 + 3];
      if (Alpha == kSentinel)        { ++Unwritten; }
      else if (Alpha == kShaderAlpha) { ++Written; }
    }

    printf("  draw error 0x%04x, read error 0x%04x\n", DrawErr, ReadErr);
    printf("  written %zu / %zu texels (%.2f%%), still sentinel %zu\n\n",
           Written, Total, 100.0 * (double)Written / (double)Total, Unwritten);
    fflush(stdout);
  }

  CGLSetCurrentContext(0);
  CGLDestroyContext(Context);
  CGLDestroyPixelFormat(PixelFormat);
  return 0;
}
