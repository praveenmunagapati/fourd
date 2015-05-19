#include <math.h>

#include "physics.h"
#include "physics_help.h"
#include "quaxol.h"

namespace fd {

void Physics::Step(float fDelta) {
}

// returns true if hit, overwrites distance with amount if so.
bool Physics::RayCast(
    const Vec4f& position, const Vec4f& ray, float* outDistance) {

  if(m_chunk) {
    if(RayCastChunk(*m_chunk, position, ray, outDistance)) {
      return true;
    }
  }
  return RayCastGround(position, ray, outDistance);
}

bool Physics::RayCastGround(
    const Vec4f& position, const Vec4f& ray, float* outDistance) {
  return PhysicsHelp::RayToPlane(position, ray,
      m_groundNormal, m_groundHeight,
      NULL /* outCollisionPoint */, outDistance);
}

void Physics::ClampToGround(Vec4f* position) {
  float dotGround = m_groundNormal.dot(*position);
  if(dotGround < 0.0f) {
    *position -= (m_groundNormal * dotGround);
  }
}

bool Physics::RayCastChunk(const QuaxolChunk& chunk,
    const Vec4f& position, const Vec4f& ray, float* outDistance) {
  assert(ray.length() > 0.0f);

  Vec4f localPos(position);
  localPos -= chunk.m_position;
  
  Vec4f localRay(ray);
  
  // scale down from boxsize to allow integer local coords
  // actually why are we doing non-integer stuff anyway??
  // oh right, didn't start with quaxols... probably should change that
  // or forever suffer.
  localPos /= chunk.m_blockSize;
  localRay /= chunk.m_blockSize;

  Vec4f chunkMin(0.0f, 0.0f, 0.0f, 0.0f);
  Vec4f chunkMax((float)chunk.m_blockDims.x, (float)chunk.m_blockDims.y,
      (float)chunk.m_blockDims.z, (float)chunk.m_blockDims.w);
  const float edgeThreshold = 0.000001f; // ok these are getting silly...
  Vec4f floatThresholdMax(edgeThreshold, edgeThreshold, edgeThreshold, edgeThreshold);
  chunkMax -= floatThresholdMax;
  // to avoid array checks on every block step, clip the ray to the possible
  // array bounds beforehand
  Vec4f unclippedLocalPos = localPos;
  Vec4f clippedPos;
  if(PhysicsHelp::RayToAlignedBox(chunkMin, chunkMax, localPos, localRay,
      NULL /*dist*/, &clippedPos)) {
    // if the start isn't within the box, clip that first
    if(!PhysicsHelp::WithinBox(chunkMin, chunkMax, localPos)) {
      Vec4f normalLocalRay = localRay.normalized();
      localPos = clippedPos + (normalLocalRay * edgeThreshold);
    }

    // if the end isn't within the box, clip that
    if(!PhysicsHelp::WithinBox(chunkMin, chunkMax, localPos + localRay)) {
      Vec4f clippedEnd;
      // Need to clip ray too so raycast backward. Almost certainly already
      // calculated and discarded the result.
      if(!PhysicsHelp::RayToAlignedBox(chunkMin, chunkMax, localPos + localRay, -localRay,
          NULL /*dist*/, &clippedEnd)) {
        return false; // ???
      }
      Vec4f normalUnclippedRay = localRay.normalized();
      localRay = clippedEnd - localPos;
      localRay *= 0.9999f; // shorten it a bit to avoid going over

      Vec4f normalLocalRay = localRay.normalized();
      //assert(normalUnclippedRay.approxEqual(normalLocalRay, 0.000001f));
    }
  } else {
    // The ray didn't hit the bounding box, so make sure the start is within
    // or we are done because the whole chunk was missed.
    if(!PhysicsHelp::WithinBox(chunkMin, chunkMax, localPos)) {
      return false;
    }
  }

  // This does a stepping algorithm checking against quaxols in the ray path.
  Vec4f localHitPos;
  if(!LocalRayCastChunk(chunk, localPos, localRay, &localHitPos)) {
    return false;
  }
  //localHitPos *= chunk.m_blockSize;
  //localHitPos += chunk.m_position;

  if(outDistance) {
    Vec4f hitDelta = (localHitPos - unclippedLocalPos) * chunk.m_blockSize;
    *outDistance = abs(hitDelta.length());
  }

  return true;
}

inline int GetSign(float val) {
  if(signbit(val)) return -1;
  else return 1;
}

inline void ClampToInteger(const Vec4f& position, const int (&signs)[4],
    Vec4f* outClampDelta, QuaxolSpec* outClampPos) {
  Vec4f clampPos;
  for (int c = 0; c < 4; c++) {
    clampPos[c] = (signs[c] < 0) ? ceil(position[c]) : floor(position[c]);
  }
  if(outClampDelta) {
    *outClampDelta = clampPos - position;
  }
  if(outClampPos) {
    *outClampPos = QuaxolSpec(clampPos);
  }
}

bool Physics::LocalRayCastChunk(const QuaxolChunk& chunk,
    const Vec4f& start, const Vec4f& ray, Vec4f* outPos) {

  int sign[4];
  for(int c = 0; c < 4; c++) {
    sign[c] = GetSign(ray[c]);
  }

  Vec4f normal = ray.normalized();

  Vec4f clampDir;
  ClampToInteger(start, sign, &clampDir, NULL /*gridPos*/);
  Vec4f step;
  Vec4f stepCounter;
  for(int c = 0; c < 4; c++) {
    if (normal[c] != 0.0f) {
      // we will add this to the counter whenever we take a step
      step[c] = abs(1.0f / normal[c]);
       // start out with the right number of "steps" based on start position
      stepCounter[c] = (1.0f - abs(clampDir[c])) * step[c];
    } else {
      step[c] = 0.0f;
      stepCounter[c] = FLT_MAX;
    }
  }

  QuaxolSpec gridPos(start);
  QuaxolSpec gridEnd(start + ray);
  
  // do the start
  if(chunk.IsPresent(gridPos[0], gridPos[1], gridPos[2], gridPos[3])) {
    // Actually back up the start by a little bit so if we had beed clipped
    // to avoid a chunk memory out of bounds error, we still hit correctly.
    const float shiftAmount = 0.01f;
    Vec4f shiftedStart = start - (normal * shiftAmount);
    Vec4f shiftedRay = ray + (normal * (2.0f * shiftAmount));
    if(!PhysicsHelp::RayToQuaxol(
        gridPos, shiftedStart, shiftedRay, NULL /*outDist*/, outPos)) {
      // weird, but we really need to write to outPos
      assert(false);
      *outPos = start;
    }
    return true;
  }

  while(gridPos != gridEnd) {
    int nextAxis;
    float smallestStep = FLT_MAX;
    for(int c = 0; c < 4; c++) {
      if(stepCounter[c] < smallestStep) {
        nextAxis = c;
        smallestStep = stepCounter[c];
      }
    }

    gridPos[nextAxis] += sign[nextAxis];
    stepCounter[nextAxis] += step[nextAxis];
    if(chunk.IsPresent(gridPos[0], gridPos[1], gridPos[2], gridPos[3])
        && PhysicsHelp::RayToQuaxol(gridPos, start, ray, NULL /*outDist*/, outPos)) {
      return true;
    }
  }

  return false;
}

void Physics::LineDraw4D(const Vec4f& start, const Vec4f& ray,
    DelegateN<void, int, int, int, int, const Vec4f&, const Vec4f&> callback) {
  
  int sign[4];
  for(int c = 0; c < 4; c++) {
    sign[c] = GetSign(ray[c]);
  }

  Vec4f normal = ray.normalized();

  Vec4f clampDir;
  ClampToInteger(start, sign, &clampDir, NULL /*gridPos*/);
  Vec4f step;
  Vec4f stepCounter;
  for(int c = 0; c < 4; c++) {
    if (normal[c] != 0.0f) {
      // we will add this to the counter whenever we take a step
      step[c] = abs(1.0f / normal[c]);
       // start out with the right number of "steps" based on start position
      stepCounter[c] = (1.0f - abs(clampDir[c])) * step[c];
    } else {
      step[c] = 0.0f;
      stepCounter[c] = FLT_MAX;
    }
  }

  QuaxolSpec gridPos(start);
  QuaxolSpec gridEnd(start + ray);
  callback(gridPos[0], gridPos[1], gridPos[2], gridPos[3], start, ray);

  while(gridPos != gridEnd) {
    int nextAxis;
    float smallestStep = FLT_MAX;
    for(int c = 0; c < 4; c++) {
      if(stepCounter[c] < smallestStep) {
        nextAxis = c;
        smallestStep = stepCounter[c];
      }
    }

    gridPos[nextAxis] += sign[nextAxis];
    stepCounter[nextAxis] += step[nextAxis];
    callback(gridPos[0], gridPos[1], gridPos[2], gridPos[3], start, ray);
  }
}

TVecQuaxol Physics::s_testQuaxols;
void Physics::TestPhysicsCallback(
    int x, int y, int z, int w, const Vec4f& position, const Vec4f& ray) {
  s_testQuaxols.emplace_back(x, y, z, w);
  assert(true == PhysicsHelp::RayToQuaxol(
      QuaxolSpec(x, y, z, w), position, ray, NULL /*dist*/, NULL /*outPoint*/));
  const int infiniteCheck = 100;
  assert(x >= -infiniteCheck && x < infiniteCheck);
  assert(y >= -infiniteCheck && y < infiniteCheck);
  assert(z >= -infiniteCheck && z < infiniteCheck);
  assert(w >= -infiniteCheck && w < infiniteCheck);
}

void Physics::TestPhysics() {
  Physics physTest;

  ////////////////////////
  // Basic raycast
  Vec4f posTest(1.0f, 1.0f, 1.0f, 1.0f);
  Vec4f rayDown(0.0f, 0.0f, -10.0f, 0.0f);
  float dist = 0.0f;
  assert(true == physTest.RayCastGround(posTest, rayDown, &dist));
  assert(dist == 1.0f); // may need to do some float threshold compares

  posTest.set(20.0f, 0.0f, 11.0f, 0.0f);
  assert(false == physTest.RayCastGround(posTest, rayDown, &dist));

  posTest.set(10.0f, 0.0f, 3.0f, 0.0f);
  rayDown.set(8.0f, 0.0f, -6.0f, 0.0f);
  assert(true == physTest.RayCastGround(posTest, rayDown, &dist));
  assert(dist == 5.0f);

  /////////////////////
  // callback sanity
  DelegateN<void, int, int, int, int, const Vec4f&, const Vec4f&> stepCallback;
  stepCallback.Bind(Physics::TestPhysicsCallback);
  Vec4f pos(1.5f, 2.5f, 0.0f, 0.0f);
  Vec4f ray(2.0f, 0.0f, 0.0f, 0.0f);
  stepCallback(1, 2, 0, 0, pos, ray);
  assert(s_testQuaxols.size() == 1);
  assert(s_testQuaxols[0].x == 1 && s_testQuaxols[0].y == 2);

  ////////////////////
  // LineDraw2D, which was along the way to 4d
  // as it's a nice algorithm
  s_testQuaxols.resize(0);
  pos.set(1.5f, 1.5f, 0.0f, 0.0f);
  ray.set(2.0f, 0.0f, 0.0f, 0.0f);
  physTest.LineDraw4D(pos, ray, stepCallback); 
  assert(s_testQuaxols.size() == 3);
  assert(s_testQuaxols[0].x == 1 && s_testQuaxols[0].y == 1);
  assert(s_testQuaxols[1].x == 2 && s_testQuaxols[1].y == 1);
  assert(s_testQuaxols[2].x == 3 && s_testQuaxols[2].y == 1);

  s_testQuaxols.resize(0);
  pos.set(1.5f, 1.5f, 0.0f, 0.0f);
  ray.set(-2.0f, 1.0f, 0.0f, 0.0f); // draw from (1.5,1.5) to (-0.5,2.5)
  physTest.LineDraw4D(pos, ray, stepCallback); 
  assert(s_testQuaxols.size() == 4); //(1,1), (0,1), (-1,1), (-1,2)
  assert(s_testQuaxols[0].x == 1 && s_testQuaxols[0].y == 1);
  //assert(s_testQuaxols[1].x == 2 && s_testQuaxols[1].y == 1);
  assert(s_testQuaxols[3].x == -1 && s_testQuaxols[3].y == 2);

  s_testQuaxols.resize(0);
  pos.set(1.5f, 1.5f, 0.0f, 0.0f);
  ray.set(-1.0f, -2.0f, 0.0f, 0.0f); // draw from (1.5,1.5) to (0.5,-0.5)
  physTest.LineDraw4D(pos, ray, stepCallback); 
  assert(s_testQuaxols.size() == 4); //(1,1), (1,0), (1,-1)
  assert(s_testQuaxols[0].x == 1 && s_testQuaxols[0].y == 1);
  //assert(s_testQuaxols[1].x == 2 && s_testQuaxols[1].y == 1);
  assert(s_testQuaxols[3].x == 0 && s_testQuaxols[3].y == -1);

  ///////////////////
  // 4D drawing
  pos.set(1.5f, 1.5f, 1.5f, 1.5f);
  ray.set(0.0f, 0.0f, 0.0f, 2.0f);
  s_testQuaxols.resize(0);
  physTest.LineDraw4D(pos, ray, stepCallback);
  assert(s_testQuaxols.size() == 3);
  assert(s_testQuaxols[0].w == 1 && s_testQuaxols[0].y == 1);
  assert(s_testQuaxols[1].w == 2 && s_testQuaxols[1].y == 1);
  assert(s_testQuaxols[2].w == 3 && s_testQuaxols[2].y == 1);

  pos.set(1.5f, 1.5f, 1.5f, 1.5f);
  ray.set(0.0f, 1.0f, 0.0f, -2.0f);
  s_testQuaxols.resize(0);
  physTest.LineDraw4D(pos, ray, stepCallback);
  assert(s_testQuaxols.size() == 4); //(1,1,1,1), (1,1,1,0), (1,1,1,-1), (1,2,1,-1)
  assert(s_testQuaxols[0] == QuaxolSpec(1,1,1,1));
  assert(s_testQuaxols[3] == QuaxolSpec(1,2,1,-1));

  pos.set(1.5f, 1.5f, 1.5f, 1.5f);
  ray.set(-2.5f, 1.0f, 1.7f, -2.0f);
  s_testQuaxols.resize(0);
  physTest.LineDraw4D(pos, ray, stepCallback);
  assert(s_testQuaxols.size() == 8);
  assert(s_testQuaxols[0] == QuaxolSpec(1,1,1,1));
  assert(s_testQuaxols[7] == QuaxolSpec(-1,2,3,-1));

  pos.set(0.0500000007f, 0.0500000007f, 1.54999995f, 0.449999988f);
  ray.set(0.319010586f, 15.9484043f, 7.15184228e-007f, 0.000000000f);
  s_testQuaxols.resize(0);
  physTest.LineDraw4D(pos, ray, stepCallback);
  // just need to make sure it terminates

  Vec4f chunkPos(0.0f, 0.0f, 0.0f, 0.0f);
  Vec4f chunkBlockSize(10.0f, 10.0f, 10.0f, 10.0f); //dumb to even support
  QuaxolChunk testChunk(chunkPos, chunkBlockSize);
  TVecQuaxol quaxols;
  quaxols.emplace_back(1, 1, 1, 1);
  assert(true == testChunk.LoadFromList(&quaxols, NULL /*offset*/));

  pos.set(25.0f, 25.0f, 25.0f, 25.0f);
  ray.set(-10.0f, -10.0f, -11.0f, -12.0f);
  assert(true == physTest.RayCastChunk(testChunk, pos, ray, NULL));

  quaxols.emplace_back(1, 2, 1, 1);
  quaxols.emplace_back(2, 1, 1, 1);
  quaxols.emplace_back(2, 2, 1, 1);
  quaxols.emplace_back(1, 1, 2, 1);
  quaxols.emplace_back(1, 1, 3, 1);
  quaxols.emplace_back(1, 1, 4, 1);
  quaxols.emplace_back(2, 2, 0, 2);
  assert(true == testChunk.LoadFromList(&quaxols, NULL /*offset*/));
  pos.set(25.0f, 25.0f, 25.0f, 25.0f);
  ray.set(-10.0f, -10.0f, 11.0f, -12.0f);
  float hitDist = -1.0f;
  assert(true == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));
  assert(hitDist > 0.0f);

  ray.set(-1000.0f, -1000.0f, 1100.0f, -1200.0f);
  assert(true == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));
  assert(hitDist > 0.0f);

  ray.set(-1000.0f, 0.01f, 0.01f, 0.01f);
  assert(false == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));

  ray.set(0.01f, 0.01f, 0.01f, 0.01f);
  assert(false == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));

  ray.set(0.01f, 0.01f, -10000.01f, 0.01f);
  assert(true == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));
  assert(hitDist > 0.0f);

  pos.set(0.500000007f, 0.500000007f, 15.4999995f, 4.49999988f);
  ray.set(3.19010586f, 159.484043f, 7.15184228e-006f, 0.000000000f);
  assert(false == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));

  // where do all these come frome? these are steps along the apparently long
  // road of writing a 4d raycaster that doesn't fuck up.
  quaxols.emplace_back(2, 0, 3, 0);
  assert(true == testChunk.LoadFromList(&quaxols, NULL /*offset*/));
  pos.set(25.7389202f, 9.39026356f, 36.6651955f, 4.50000000f);
  ray.set(-432.569427f, -739.241638f, -516.141785f, -0.000000000f);
  assert(true == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));
  
  pos.set(0.500032365f, 82.7353592f, 142.041000f, 167.218506f);
  ray.set(-0.000243353134f, 426.009521f, -328.929626f, -842.804993f);
  physTest.RayCastChunk(testChunk, pos, ray, &hitDist);
  // just that it didn't crash

  pos.set(-0.774945676f, 34.8522301f, 39.2764893f, 108.281807f);
  ray.set(780.614685f, -79.4120407f, -619.972534f, 0.000123921200f);
  physTest.RayCastChunk(testChunk, pos, ray, &hitDist);
  // just that it didn't crash

  //quaxols.emplace_back(12, 10, 13, 10);
  //assert(true == testChunk.LoadFromList(&quaxols, NULL /*offset*/));
  //pos.set(140.621994f, 129.451874f, 131.667252f, 152.824280f);
  //ray.set(-846.823059f, 495.832581f, 192.447861f, 0.00135995192f);
  //assert(true == physTest.RayCastChunk(testChunk, pos, ray, &hitDist));
}

} // namespace fd