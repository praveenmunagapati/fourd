#include <memory>
#include "../common/frame_timer.h"
#include "../common/mesh.h"
#include "../common/components/animated_rotation.h"
#include "../common/components/mesh_cleanup.h"
#include "../common/components/timed_death.h"
#include "entity.h"
#include "render_helper.h"
#include "render.h"
#include "scene.h"

extern fd::Render g_renderer; // fourd.cpp

namespace fd {

typedef std::list<std::unique_ptr<Mesh>> MeshList;
MeshList g_eyeCandyMeshes;

void RenderHelper::AddEyeCandy(EyeCandyTypes type, const Vec4f& pos) {
  g_eyeCandyMeshes.emplace_back(new Mesh());
  Mesh* candy = g_eyeCandyMeshes.back().get();
  const float size = 30.0f;
  const float smallSize = 15.0f;
  Vec4f smallOff(13.0f, 0.0f, 0.0f, 3.0f);
  switch(type) {
    case EyeCandyQuad:
      candy->buildQuad(smallSize, smallOff, Vec4f());
      break;
    case EyeCandyCube:
      candy->buildCube(smallSize, smallOff, Vec4f());
      break;
    case EyeCandyTesseract:
      candy->buildTesseract(smallSize, smallOff, Vec4f());
      break;
    case EyeCandy16Cell:
      candy->build16cell(size, Vec4f());
      break;
    case EyeCandy24Cell: {
      const float shift = 8.5f;
      const Vec4f offsetSize(shift, shift, 0.5f, shift * 2);
      candy->buildCaylay24Cell(size, offsetSize);
    } break;
    case EyeCandy120Cell: {
      const float shift = 5.5f;
      const Vec4f offsetSize(size / 2 + shift, size / 2 + shift, shift, size / 2 + shift);
      candy->buildCaylay120Cell(size, offsetSize);
    } break;
    case EyeCandy600Cell: {
      const float shift = 5.5f;
      const Vec4f offsetSize(size / 2 + shift, size / 2 + shift, shift, size / 2 + shift);
      candy->buildCaylay600Cell(size, offsetSize);
    } break;
  }

  Entity* pEntity = g_renderer.GetFirstScene()->AddEntity();
  // ugh need like a mesh manager and better approach to shader handling
  //pEntity->Initialize(candy, LoadShader("ColorBlend"), NULL);
  pEntity->Initialize(candy, g_renderer.LoadShader("ColorBlendClipped"), NULL);
  pEntity->m_position = pos;
  pEntity->GetComponentBus().AddComponent(
      new AnimatedRotation((float)PI * 2.0f, Camera::RIGHT, Camera::INSIDE,
      -20.0f /* ugh negative duration to be forever*/, true /*worldRotation*/));
}

Entity* RenderHelper::RenderTess(Vec4f pos, const Mat4f* rotation, Vec4f color, float scale) {
  //SCOPE_TIME(); //"RenderHelper::RenderAxis");
  static Mesh staticTesseract;
  static bool setupAlready = false;
  if(!setupAlready) {
    staticTesseract.buildTesseract(1.0f, Vec4f(), Vec4f());
    setupAlready = true;
  }

  std::unique_ptr<Mesh> tesseract(new Mesh());
  tesseract->merge(staticTesseract);  
  // TODO: generalize this or make it fast enough to call like this
  //tesseract->buildTesseract(1.0f, Vec4f(), Vec4f());
  tesseract->fillSolidColors(color);
  Mesh* pMeshToLoad = tesseract.get();

  Entity* pEntity = g_renderer.GetFirstScene()->AddEntity();
  pEntity->Initialize(pMeshToLoad, g_renderer.LoadShader("VolumeColor"), NULL);
  pEntity->m_position = pos;
  if(rotation) {
    pEntity->m_orientation = *rotation;
  }
  pEntity->m_orientation = pEntity->m_orientation * scale;
  pEntity->GetComponentBus().AddComponent(new MeshCleanupComponent(&(pEntity->m_pMesh)));
  if(pEntity->m_pMesh) {
    tesseract.release(); // as now the MeshCleanupComponent has it
  }
  return pEntity;
}

void RenderHelper::RenderAxis(Vec4f pos, const Mat4f* rotation, float scale, bool permanent) {
  //SCOPE_TIME(); //"RenderHelper::RenderAxis");
  Vec4f placeAt;
  Vec4f colors[] = {
      Vec4f(1.0f, 0.0f, 0.0f, 0.0f),
      Vec4f(0.0f, 1.0f, 0.0f, 0.0f),
      Vec4f(0.0f, 0.0f, 1.0f, 0.0f),
      Vec4f(1.0f, 0.0f, 1.0f, 0.0f) };

  for(int dir = 0; dir < 4; dir++) { // direction
    Vec4f direction(0.0f, 0.0f, 0.0f, 0.0f);
    Vec4f scaleDiag(1.0f, 1.0f, 1.0f, 1.0f);
    for(int i = 0; i < 4; i++) { // when are you going to make this thing n-dimensional, anyway?
      if(dir == i) {
        direction[dir] = scale * 0.2f; // arbitrary spacing
        scaleDiag[dir] = scale;
      }
    }

    placeAt = pos;
    if(rotation) {
      placeAt += rotation->transform(direction);
    } else {
      placeAt += direction;
    }
    Mat4f placeLike;
    if(rotation) {
      placeLike = *rotation;
    }
    placeLike = placeLike * Mat4f().storeScale(scaleDiag);
    Entity* tess = RenderHelper::RenderTess(placeAt, &placeLike, colors[dir]);
    if(!permanent && tess) {
      tess->GetComponentBus().AddComponent(new TimedDeath(10.1f)); //uuuugh
    }
  }
}
// so according to this, the render matrix is
// +x right, +y up, -z forward, +w in
// camera matrix is -x right, +y up, +z forward, +w in
// hahaha

float CenteredRand() { return ((float)rand() / (float)RAND_MAX) - 0.5f; }
void RenderHelper::SpamAxes(Vec4f pos) {
  //SCOPE_TIME(); //"RenderHelper::SpamAxes");
  static float randSpread = 1000.0f;
  static int num = 1;
  static Vec4f fixedOffset(0.0f, 0.0f, 0.0f, 0.0f);
  pos += fixedOffset;
  for(int i = 0; i <= num; i++) {
    Vec4f randOffset(CenteredRand(), CenteredRand(), CenteredRand(), CenteredRand());
    randOffset = (randOffset * randSpread) + pos;
    RenderAxis(randOffset, NULL /*rot*/, 20.0f /*scale*/, false /*permanent*/);
  }
}

} // namespace fd 