/*
 *  Copyright (C) 1998-2000 Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#define __STDC_LIMIT_MACROS

#include <kodi/addon-instance/Visualization.h>
#include <kodi/gui/gl/GL.h>
#include <kodi/gui/gl/Shader.h>

#include <string.h>
#include <math.h>
#include <stdint.h>
#include <cstddef>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef M_PI
#define M_PI 3.141592654f
#endif

#define NUM_BANDS 64

class ATTR_DLL_LOCAL CVisualizationSpectrum
  : public kodi::addon::CAddonBase,
    public kodi::addon::CInstanceVisualization,
    public kodi::gui::gl::CShaderProgram
{
public:
  CVisualizationSpectrum();
  ~CVisualizationSpectrum() override = default;

  bool Start(int channels, int samplesPerSec, int bitsPerSample, const std::string& songName) override;
  void Stop() override;
  void Render() override;
  void AudioData(const float* audioData, size_t audioDataLength) override;
  ADDON_STATUS SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue) override;

  void OnCompiledAndLinked() override;
  bool OnEnabled() override;

private:
  void SetBarHeightSetting(int settingValue);
  void SetSpeedSetting(int settingValue);

  GLfloat m_heights[NUM_BANDS];
  GLfloat m_cHeights[NUM_BANDS];
  GLfloat m_scale;
  float m_hSpeed;

  void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b);
  void get_rainbow_color(float position, float& r, float& g, float& b);
  void draw_spectrum_bars(void);

  glm::mat4 m_projMat;
  glm::mat4 m_modelMat;
  std::vector<glm::vec3> m_vertex_buffer_data;
  std::vector<glm::vec3> m_color_buffer_data;

#ifdef HAS_GL
  GLuint m_vertexVBO[2] = {0};
#endif

  GLint m_uProjMatrix = -1;
  GLint m_uModelMatrix = -1;
  GLint m_uPointSize = -1;
  GLint m_hPos = -1;
  GLint m_hCol = -1;

  bool m_startOK = false;
};

CVisualizationSpectrum::CVisualizationSpectrum()
  : m_hSpeed(0.05f)
{
  m_scale = 1.0 / log(256.0);

  SetBarHeightSetting(kodi::addon::GetSettingInt("bar_height"));
  SetSpeedSetting(kodi::addon::GetSettingInt("speed"));

  memset(m_heights, 0, sizeof(m_heights));
  memset(m_cHeights, 0, sizeof(m_cHeights));
}

bool CVisualizationSpectrum::Start(int channels, int samplesPerSec, int bitsPerSample, const std::string& songName)
{
  (void)channels;
  (void)samplesPerSec;
  (void)bitsPerSample;
  (void)songName;

  std::string fraqShader = kodi::addon::GetAddonPath("resources/shaders/" GL_TYPE_STRING "/frag.glsl");
  std::string vertShader = kodi::addon::GetAddonPath("resources/shaders/" GL_TYPE_STRING "/vert.glsl");
  if (!LoadShaderFiles(vertShader, fraqShader) || !CompileAndLink())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to create or compile shader");
    return false;
  }

  for(int i = 0; i < NUM_BANDS; i++)
  {
    m_heights[i] = 0.0f;
    m_cHeights[i] = 0.0f;
  }

  m_projMat = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

#ifdef HAS_GL
  glGenBuffers(2, m_vertexVBO);
#endif

  m_startOK = true;
  return true;
}

void CVisualizationSpectrum::Stop()
{
  if (!m_startOK)
    return;

  m_startOK = false;

#ifdef HAS_GL
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(2, m_vertexVBO);
  m_vertexVBO[0] = 0;
  m_vertexVBO[1] = 0;
#endif
}

void CVisualizationSpectrum::Render()
{
  if (!m_startOK)
    return;

#ifdef HAS_GL
  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[0]);
  glVertexAttribPointer(m_hPos, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3, nullptr);
  glEnableVertexAttribArray(m_hPos);

  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[1]);
  glVertexAttribPointer(m_hCol, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3, nullptr);
  glEnableVertexAttribArray(m_hCol);
#else
  glEnableVertexAttribArray(m_hPos);
  glVertexAttribPointer(m_hPos, 3, GL_FLOAT, GL_FALSE, 0, &m_vertex_buffer_data[0]);

  glEnableVertexAttribArray(m_hCol);
  glVertexAttribPointer(m_hCol, 3, GL_FLOAT, GL_FALSE, 0, &m_color_buffer_data[0]);
#endif

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  m_modelMat = glm::mat4(1.0f);

  EnableShader();

  draw_spectrum_bars();

  DisableShader();

  glDisableVertexAttribArray(m_hPos);
  glDisableVertexAttribArray(m_hCol);

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
}

void CVisualizationSpectrum::OnCompiledAndLinked()
{
  m_uProjMatrix = glGetUniformLocation(ProgramHandle(), "u_projectionMatrix");
  m_uModelMatrix = glGetUniformLocation(ProgramHandle(), "u_modelViewMatrix");
  m_uPointSize = glGetUniformLocation(ProgramHandle(), "u_pointSize");
  m_hPos = glGetAttribLocation(ProgramHandle(), "a_position");
  m_hCol = glGetAttribLocation(ProgramHandle(), "a_color");
}

bool CVisualizationSpectrum::OnEnabled()
{
  glUniformMatrix4fv(m_uProjMatrix, 1, GL_FALSE, glm::value_ptr(m_projMat));
  glUniformMatrix4fv(m_uModelMatrix, 1, GL_FALSE, glm::value_ptr(m_modelMat));

  return true;
}

void CVisualizationSpectrum::hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b)
{
  float c = (1.0f - ::fabs(2.0f * l - 1.0f)) * s;
  float x = c * (1.0f - ::fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
  float m = l - c / 2.0f;

  float rp = 0, gp = 0, bp = 0;

  if (h >= 0 && h < 60) { rp = c; gp = x; bp = 0; }
  else if (h >= 60 && h < 120) { rp = x; gp = c; bp = 0; }
  else if (h >= 120 && h < 180) { rp = 0; gp = c; bp = x; }
  else if (h >= 180 && h < 240) { rp = 0; gp = x; bp = c; }
  else if (h >= 240 && h < 300) { rp = x; gp = 0; bp = c; }
  else if (h >= 300 && h < 360) { rp = c; gp = 0; bp = x; }

  r = rp + m;
  g = gp + m;
  b = bp + m;
}

void CVisualizationSpectrum::get_rainbow_color(float position, float& r, float& g, float& b)
{
  float hue = position * 360.0f;
  hsl_to_rgb(hue, 1.0f, 0.5f, r, g, b);
}

void CVisualizationSpectrum::draw_spectrum_bars(void)
{
  const int numBars = NUM_BANDS;
  const float barWidth = 1.8f / numBars;
  const float barSpacing = 0.005f;
  const float actualBarWidth = barWidth - barSpacing;
  const int segmentsPerBar = 32;
  const float segmentHeight = 2.0f / segmentsPerBar;
  const float startX = -0.9f;
  const float startY = -0.95f;

  m_vertex_buffer_data.clear();
  m_color_buffer_data.clear();

  for (int bar = 0; bar < numBars; bar++)
  {
    float height = m_cHeights[bar];
    if (height < 0.001f)
      height = 0.001f;

    float maxVisualHeight = 1.9f;
    float visualHeight = height * maxVisualHeight;
    if (visualHeight > maxVisualHeight)
      visualHeight = maxVisualHeight;

    int numSegments = (int)(visualHeight / segmentHeight);
    if (numSegments < 1) numSegments = 1;
    if (numSegments > segmentsPerBar) numSegments = segmentsPerBar;

    float barX = startX + bar * barWidth;

    float baseR, baseG, baseB;
    get_rainbow_color((float)bar / (float)(numBars - 1), baseR, baseG, baseB);

    for (int seg = 0; seg < numSegments; seg++)
    {
      float segY = startY + seg * segmentHeight;
      float segH = segmentHeight;
      if (seg == numSegments - 1)
      {
        float totalH = numSegments * segmentHeight;
        if (totalH < visualHeight)
          segH = visualHeight - (numSegments - 1) * segmentHeight;
      }

      float intensityFactor = 0.3f + 0.7f * ((float)seg / (float)segmentsPerBar);
      float r = baseR * intensityFactor;
      float g = baseG * intensityFactor;
      float b = baseB * intensityFactor;

      float x0 = barX;
      float y0 = segY;
      float x1 = barX + actualBarWidth;
      float y1 = segY + segH;

      m_vertex_buffer_data.push_back({x0, y0, 0.0f});
      m_vertex_buffer_data.push_back({x1, y0, 0.0f});
      m_vertex_buffer_data.push_back({x1, y1, 0.0f});

      m_vertex_buffer_data.push_back({x0, y0, 0.0f});
      m_vertex_buffer_data.push_back({x1, y1, 0.0f});
      m_vertex_buffer_data.push_back({x0, y1, 0.0f});

      for (int v = 0; v < 6; v++)
      {
        m_color_buffer_data.push_back({r, g, b});
      }
    }
  }

  if (!m_vertex_buffer_data.empty())
  {
#ifdef HAS_GL
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[0]);
    glBufferData(GL_ARRAY_BUFFER, m_vertex_buffer_data.size()*sizeof(glm::vec3), &m_vertex_buffer_data[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[1]);
    glBufferData(GL_ARRAY_BUFFER, m_color_buffer_data.size()*sizeof(glm::vec3), &m_color_buffer_data[0], GL_STATIC_DRAW);
#endif
    glDrawArrays(GL_TRIANGLES, 0, m_vertex_buffer_data.size());
  }
}

void CVisualizationSpectrum::AudioData(const float* pAudioData, size_t iAudioDataLength)
{
  int xscale[] = {0, 1, 2, 3, 5, 7, 10, 14, 20, 28, 40, 54, 74, 101, 137, 187, 255,
                  256, 270, 290, 310, 340, 370, 400, 430, 460, 490, 520, 558, 598, 642,
                  689, 739, 792, 848, 907, 970, 1037, 1108, 1183, 1263, 1348, 1438, 1534,
                  1636, 1744, 1859, 1983, 2116, 2259, 2413, 2579, 2758, 2952, 3161, 3388,
                  3635, 3904, 4197, 4517, 4866, 5247, 5664, 6121, 6622, 7173, 7778, 8444};

  for(int y = NUM_BANDS - 1; y > 0; y--)
  {
    m_heights[y] = m_heights[y - 1];
  }

  for(int i = 0; i < NUM_BANDS; i++)
  {
    int y = 0;
    int startIdx = (i < 17) ? xscale[i] : xscale[16] + (i - 17) * ((xscale[63] - xscale[16]) / 47);
    int endIdx = (i < 17) ? xscale[i + 1] : xscale[16] + (i - 16) * ((xscale[63] - xscale[16]) / 47);

    for(int c = startIdx; c < endIdx && c < (int)iAudioDataLength; c++)
    {
      int val = (int)(pAudioData[c] * (INT16_MAX));
      if(val > y)
        y = val;
    }

    y >>= 7;
    float val;
    if(y > 0)
      val = (logf(y) * m_scale);
    else
      val = 0;

    m_heights[i] = val;
  }

  for(int i = 0; i < NUM_BANDS; i++)
  {
    if (::fabs(m_cHeights[i] - m_heights[i]) > m_hSpeed)
    {
      if (m_cHeights[i] < m_heights[i])
        m_cHeights[i] += m_hSpeed;
      else
        m_cHeights[i] -= m_hSpeed;
    }
    else
    {
      m_cHeights[i] = m_heights[i];
    }
  }
}

void CVisualizationSpectrum::SetBarHeightSetting(int settingValue)
{
  switch (settingValue)
  {
  case 1:
    m_scale = 1.f / log(256.f);
    break;

  case 2:
    m_scale = 2.f / log(256.f);
    break;

  case 3:
    m_scale = 3.f / log(256.f);
    break;

  case 4:
    m_scale = 0.33f / log(256.f);
    break;

  case 0:
  default:
    m_scale = 0.5f / log(256.f);
    break;
  }
}

void CVisualizationSpectrum::SetSpeedSetting(int settingValue)
{
  switch (settingValue)
  {
  case 1:
    m_hSpeed = 0.025f;
    break;

  case 2:
    m_hSpeed = 0.0125f;
    break;

  case 3:
    m_hSpeed = 0.1f;
    break;

  case 4:
    m_hSpeed = 0.2f;
    break;

  case 0:
  default:
    m_hSpeed = 0.05f;
    break;
  }
}

ADDON_STATUS CVisualizationSpectrum::SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  if (settingName.empty() || settingValue.empty())
    return ADDON_STATUS_UNKNOWN;

  if (settingName == "bar_height")
  {
    SetBarHeightSetting(settingValue.GetInt());
    return ADDON_STATUS_OK;
  }
  else if (settingName == "speed")
  {
    SetSpeedSetting(settingValue.GetInt());
    return ADDON_STATUS_OK;
  }

  return ADDON_STATUS_UNKNOWN;
}

ADDONCREATOR(CVisualizationSpectrum)
