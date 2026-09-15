// gl_first_probe.cpp -- minimal repro for the macOS rendering mismatch.
//
// Question: with the engine's attribute layout, does glDrawArrays(GL_TRIANGLES, first, count) fetch
// the vertices that live at `first`?
//
// Method: the vertex buffer is filled with a known deterministic pattern, so for any vertex index i
// the expected position is computable on the CPU.  The vertex shader writes the fetched position to
// a transform-feedback varying, and the captured values are compared against the expected ones.
// No framebuffer, no window, no pixels: glEnable(GL_RASTERIZER_DISCARD) means only the vertex fetch
// and the TF write happen.
//
// Variants mirror the engine (gpu_mapped_buffer.cpp SetupVertexAttribsFor_u3d_geo_element_buffer +
// render.cpp MultiDrawIndirect):
//   v0  one buffer, GL_FLOAT x3            -- control, 12-byte stride
//   v1  engine layout, GL_BYTE x3, stride 4 -- padded (current tree)
//   v2  engine layout, GL_BYTE x3, stride 0 -- tightly packed (as it was before padding)
//   v3  v1 plus the engine's matl buffer bound with VertexAttribIPointer at the same VAO
//
// Build (both arches, to separate an Apple driver bug from a Rosetta GL-client bug):
//   clang++ -std=c++17 -O1 -o glprobe_x86 -target x86_64-apple-macos11 gl_first_probe.cpp -framework OpenGL
//   clang++ -std=c++17 -O1 -o glprobe_arm gl_first_probe.cpp -framework OpenGL

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

static const int kPosLocation = 0;
static const int kNormalLocation = 1;
static const int kColorLocation = 2;
static const int kTransEmissLocation = 3;

// Vertex count of the pattern buffer.  Big enough to cover the engine's largest observed First
// (37,824,126) with room to spare.
static const uint32_t kVertexCount = 1u << 26; // 64M

// Deterministic, positive-only byte pattern: 1..120, so it is unambiguous either as a signed or an
// unsigned byte.
static inline uint8_t PatternByte(uint32_t VertexIndex, int Channel)
{
  return (uint8_t)(1 + ((VertexIndex * (uint32_t)(67 + 101 * Channel) + 13u * (uint32_t)Channel) % 120u));
}

static void Expected(unsigned VertexIndex, float *Out)
{
  Out[0] = (float)PatternByte(VertexIndex, 0);
  Out[1] = (float)PatternByte(VertexIndex, 1);
  Out[2] = (float)PatternByte(VertexIndex, 2);
}

#define GL_CHECK(Where)                                                        \
  do {                                                                         \
    GLenum Err = glGetError();                                                 \
    if (Err) { printf("  GL error 0x%04x at %s\n", Err, Where); }               \
  } while (0)

static GLuint CompileProgram(const char *VertexSource)
{
  const char *FragSource =
    "#version 410 core\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(1.0); }\n";

  GLuint Vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(Vs, 1, &VertexSource, 0);
  glCompileShader(Vs);
  GLint Ok = 0;
  glGetShaderiv(Vs, GL_COMPILE_STATUS, &Ok);
  if (!Ok)
  {
    char Log[8192] = {};
    glGetShaderInfoLog(Vs, sizeof(Log) - 1, 0, Log);
    printf("FATAL: vertex shader failed to compile:\n%s\n", Log);
    exit(1);
  }

  GLuint Fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(Fs, 1, &FragSource, 0);
  glCompileShader(Fs);

  GLuint Program = glCreateProgram();
  glAttachShader(Program, Vs);
  glAttachShader(Program, Fs);

  const char *Varyings[] = { "FetchedPosition" };
  glTransformFeedbackVaryings(Program, 1, Varyings, GL_INTERLEAVED_ATTRIBS);

  glLinkProgram(Program);
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

// The shader writes the *fetched* position straight out to the TF varying, so the capture is a
// verbatim read of what the vertex fetch produced.  The other three inputs are multiplied into
// gl_Position so the driver cannot optimise them away.
static const char *VertexSource =
  "#version 410 core\n"
  "layout(location = 0) in vec3 in_Position;\n"
  "layout(location = 1) in vec3 in_Normal;\n"
  "layout(location = 2) in uint in_ColorIndex;\n"
  "layout(location = 3) in ivec2 in_TransEmiss;\n"
  "out vec3 FetchedPosition;\n"
  "void main()\n"
  "{\n"
  "  FetchedPosition = in_Position;\n"
  "  float Live = float(in_ColorIndex) + float(in_TransEmiss.x) + float(in_TransEmiss.y) + in_Normal.x;\n"
  "  gl_Position = vec4(Live * 1e-30, 0.0, 0.0, 1.0);\n"
  "}\n";

struct Variant
{
  const char *Name;
  GLenum PositionType;
  GLsizei PositionStride;   // 0 means tightly packed
  int PositionSize;
  bool BindMatl;
  bool BindNormal;
};

static const Variant Variants[] = {
  { "v0 float x3 stride 12, single buffer", GL_FLOAT, 12, 3, false, false },
  { "v1 byte  x3 stride 4  (padded, engine layout)", GL_BYTE, 4, 3, false, false },
  { "v2 byte  x3 stride 0  (packed 3, as before padding)", GL_BYTE, 0, 3, false, false },
  { "v3 v1 + matl buffer via VertexAttribIPointer", GL_BYTE, 4, 3, true, false },
  { "v4 v1 + separate normal buffer", GL_BYTE, 4, 3, false, true },
  { "v5 v1 + normal + matl (full engine layout)", GL_BYTE, 4, 3, true, true },
};

// What the engine's matl is: 2 bytes of colour index plus 2 one-byte fields.
struct matl_probe { uint16_t ColorIndex; uint8_t Transparency; uint8_t Emission; };

static const uint32_t FirstValues[] = {
  0, 1, 2, 3, 6, 100, 1000, 65535, 65536, 65537,
  1048575, 1048576, 16777215, 16777216, 16777217,
  33554431, 33554432, 37824126, 67108863 - 300,
};
static const uint32_t CountValues[] = { 3, 6, 300 };

int main(int Argc, char **Argv)
{
  const char *OnlyVariant = (Argc > 1) ? Argv[1] : 0;

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
  printf("GLSL        : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
  printf("vertex buffer: %u vertices (%u MB)\n", kVertexCount, (kVertexCount * 4u) >> 20);
  printf("\n");

  // ---- pattern data -----------------------------------------------------------------------
  // Padded byte vertices: x, y, z, 0.  The float variant reads the same bytes reinterpreted, which
  // is fine -- it only has to be *self consistent*, and for float the "expected" values come from
  // the same bytes.
  std::vector<uint8_t> Padded((size_t)kVertexCount * 4);
  std::vector<float> Floats((size_t)kVertexCount * 3);
  for (uint32_t i = 0; i < kVertexCount; ++i)
  {
    Padded[(size_t)i * 4 + 0] = PatternByte(i, 0);
    Padded[(size_t)i * 4 + 1] = PatternByte(i, 1);
    Padded[(size_t)i * 4 + 2] = PatternByte(i, 2);
    Padded[(size_t)i * 4 + 3] = 0;
    Floats[(size_t)i * 3 + 0] = (float)PatternByte(i, 0);
    Floats[(size_t)i * 3 + 1] = (float)PatternByte(i, 1);
    Floats[(size_t)i * 3 + 2] = (float)PatternByte(i, 2);
  }

  std::vector<matl_probe> Matls(kVertexCount);
  for (uint32_t i = 0; i < kVertexCount; ++i)
  {
    Matls[i].ColorIndex = (uint16_t)(i % 4096);
    Matls[i].Transparency = (uint8_t)(i % 255);
    Matls[i].Emission = (uint8_t)((i / 7) % 255);
  }

  GLuint PositionBuffer = 0, FloatBuffer = 0, NormalBuffer = 0, MatlBuffer = 0;
  glGenBuffers(1, &PositionBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, PositionBuffer);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)Padded.size(), Padded.data(), GL_STATIC_DRAW);
  GL_CHECK("upload positions");

  glGenBuffers(1, &FloatBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, FloatBuffer);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(Floats.size() * sizeof(float)), Floats.data(), GL_STATIC_DRAW);
  GL_CHECK("upload floats");

  glGenBuffers(1, &NormalBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, NormalBuffer);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)Padded.size(), Padded.data(), GL_STATIC_DRAW);
  GL_CHECK("upload normals");

  glGenBuffers(1, &MatlBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, MatlBuffer);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(Matls.size() * sizeof(matl_probe)), Matls.data(), GL_STATIC_DRAW);
  GL_CHECK("upload matl");

  // ---- transform feedback target ----------------------------------------------------------
  const uint32_t MaxCount = 300;
  GLuint FeedbackBuffer = 0;
  glGenBuffers(1, &FeedbackBuffer);
  glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, FeedbackBuffer);
  glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (GLsizeiptr)(MaxCount * 3 * sizeof(float)), 0, GL_DYNAMIC_READ);
  GL_CHECK("feedback buffer");

  GLuint Program = CompileProgram(VertexSource);

  // A framebuffer is required even with rasterization discarded: without a complete one,
  // glDrawArrays is rejected with GL_INVALID_FRAMEBUFFER_OPERATION (0x506) and captures nothing.
  GLuint ColorTexture = 0;
  glGenTextures(1, &ColorTexture);
  glBindTexture(GL_TEXTURE_2D, ColorTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  GLuint DepthBuffer = 0;
  glGenRenderbuffers(1, &DepthBuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, DepthBuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);

  GLuint Fbo = 0;
  glGenFramebuffers(1, &Fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ColorTexture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, DepthBuffer);
  GLenum FboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (FboStatus != GL_FRAMEBUFFER_COMPLETE)
  {
    printf("FATAL: framebuffer incomplete (0x%04x)\n", FboStatus);
    return 1;
  }
  GL_CHECK("framebuffer setup");

  glEnable(GL_RASTERIZER_DISCARD);

  uint32_t TotalChecks = 0, TotalMismatch = 0;



  for (size_t VariantIndex = 0; VariantIndex < sizeof(Variants) / sizeof(Variants[0]); ++VariantIndex)
  {
    const Variant &V = Variants[VariantIndex];
    if (OnlyVariant && strstr(V.Name, OnlyVariant) == 0) { continue; }

    printf("=== %s ===\n", V.Name);
    uint32_t VariantChecks = 0, VariantMismatch = 0;

    GLuint Vao = 0;
    glGenVertexArrays(1, &Vao);
    glBindVertexArray(Vao);

    GLuint PosBuffer = (V.PositionType == GL_FLOAT) ? FloatBuffer : PositionBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, PosBuffer);
    if (V.PositionType == GL_FLOAT)
    {
      glVertexAttribPointer(kPosLocation, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
    }
    else
    {
      glVertexAttribPointer(kPosLocation, V.PositionSize, V.PositionType, GL_FALSE, V.PositionStride, (void *)0);
    }
    glEnableVertexAttribArray(kPosLocation);

    if (V.BindNormal)
    {
      glBindBuffer(GL_ARRAY_BUFFER, NormalBuffer);
      glVertexAttribPointer(kNormalLocation, 3, GL_BYTE, GL_TRUE, 4, (void *)0);
      glEnableVertexAttribArray(kNormalLocation);
    }
    if (V.BindMatl)
    {
      glBindBuffer(GL_ARRAY_BUFFER, MatlBuffer);
      glVertexAttribIPointer(kColorLocation, 1, GL_SHORT, sizeof(matl_probe),
                             (void *)(size_t)offsetof(matl_probe, ColorIndex));
      glVertexAttribIPointer(kTransEmissLocation, 2, GL_BYTE, sizeof(matl_probe),
                             (void *)(size_t)offsetof(matl_probe, Transparency));
      glEnableVertexAttribArray(kColorLocation);
      glEnableVertexAttribArray(kTransEmissLocation);
    }
    GL_CHECK("vao setup");

    glUseProgram(Program);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, FeedbackBuffer);

    for (size_t FirstIndex = 0; FirstIndex < sizeof(FirstValues) / sizeof(FirstValues[0]); ++FirstIndex)
    {
      uint32_t First = FirstValues[FirstIndex];
      for (size_t CountIndex = 0; CountIndex < sizeof(CountValues) / sizeof(CountValues[0]); ++CountIndex)
      {
        uint32_t Count = CountValues[CountIndex];
        if ((uint64_t)First + Count > kVertexCount) { continue; }

        glBeginTransformFeedback(GL_TRIANGLES);
        glDrawArrays(GL_TRIANGLES, (GLint)First, (GLsizei)Count);
        glEndTransformFeedback();
        GL_CHECK("draw + TF");

        GLsync Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glClientWaitSync(Fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        glDeleteSync(Fence);

        float *Captured = (float *)glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                                                    (GLsizeiptr)(Count * 3 * sizeof(float)), GL_MAP_READ_BIT);
        if (!Captured) { printf("  first=%-9u count=%-4u  MAP FAILED\n", First, Count); continue; }

        // Compare every captured vertex against the pattern at that index.
        uint32_t Mismatched = 0;
        uint32_t FirstMismatch = 0;
        for (uint32_t i = 0; i < Count; ++i)
        {
          float Expect[3];
          Expected(First + i, Expect);
          const float *Got = Captured + i * 3;
          for (int c = 0; c < 3; ++c)
          {
            if (Expect[c] != Got[c]) { if (!Mismatched) { FirstMismatch = i; } Mismatched++; break; }
          }
        }

        VariantChecks++;
        if (Mismatched) { VariantMismatch++; }

        if (Mismatched)
        {
          float Expect[3];
          Expected(First + FirstMismatch, Expect);
          const float *Got = Captured + FirstMismatch * 3;
          printf("  first=%-9u count=%-4u  MISMATCH %u/%u  v[%u] got=(%.0f %.0f %.0f) expected=(%.0f %.0f %.0f)\n",
                 First, Count, Mismatched, Count, FirstMismatch,
                 Got[0], Got[1], Got[2], Expect[0], Expect[1], Expect[2]);
        }

        glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
      }
    }

    glBindVertexArray(0);
    glDeleteVertexArrays(1, &Vao);
    printf("  --> %s: %u of %u combinations mismatched\n\n", V.Name, VariantMismatch, VariantChecks);
    TotalChecks += VariantChecks;
    TotalMismatch += VariantMismatch;
  }

  printf("=== %u of %u (variant, first, count) combinations mismatched ===\n", TotalMismatch, TotalChecks);
  return 0;
}
