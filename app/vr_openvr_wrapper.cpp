// Heavily borrowing from openvr hellovr sample integration
#include "vr_wrapper.h"
#ifdef FD_VR_USE_OPENVR

#include <openvr.h>
#include <memory>

#include "texture.h"

//#ifdef WIN32
//#include <Windows.h>
//#endif
#include "glhelper.h"
#include "render.h"
#include "shader.h"
#include "win32_platform.h"
#include "../common/fourmath.h"
#include "../common/camera.h"

// hmm not great
extern fd::Render g_renderer; // fourd.cpp

namespace fd {

#if defined(WIN32)
  class VVRWrapper : public VRWrapper {
  public:
    PlatformWindow* m_pWindow;
    vr::IVRSystem* m_HMD;

    struct EyeRenderInfo
    {
      Texture* m_framebuffer = NULL; // used as a wrappper in rendering
      Texture* m_depthTex = NULL;
      Texture* m_renderTex = NULL;
      Texture* m_resolveTex = NULL;
      EyeRenderInfo() = default;
      ~EyeRenderInfo() {
        delete m_framebuffer;
        delete m_depthTex;
        delete m_renderTex;
        delete m_resolveTex;
      }
    };
    EyeRenderInfo m_eye[2];

    bool m_flippedEyes = true; // hahahahaha wait seriously?
    vr::TrackedDevicePose_t m_devicePoses[vr::k_unMaxTrackedDeviceCount];
    Pose4f m_device3Poses[vr::k_unMaxTrackedDeviceCount];
    Pose4f m_hmdPose;
    std::string m_strCurrentDeviceClasses;
    char m_rDeviceClassCharCodes[vr::k_unMaxTrackedDeviceCount];
    //vr::ETrackedDeviceClass m_deviceClasses[vr::k_unMaxTrackedDeviceCount];

    unsigned int m_numDistortionIndices;
    GLuint m_unLensVAO;
    GLuint m_glIDVertBuffer;
    GLuint m_glIDIndexBuffer;
    GLuint m_unLensProgramID;
    Shader* m_lensShader;

    uint32_t m_eyeRenderWidth;
    uint32_t m_eyeRenderHeight;

    //const Mat4f* m_debugHeadPose;
    //bool m_isDebugDevice

    VVRWrapper() : m_HMD(NULL), m_pWindow(NULL)
      , m_eyeRenderWidth(0), m_eyeRenderHeight(0)
      , m_numDistortionIndices(0)
      , m_lensShader(NULL)
      //    , m_debugHeadPose(NULL)
      //    , m_isDebugDevice(true)
    {
      assert(vr::Eye_Left == 0);
      assert(vr::Eye_Right== 1);
      memset(&m_rDeviceClassCharCodes[0], 0, sizeof(m_rDeviceClassCharCodes));
      m_doScreenSaver = false; // vive has a screensaver already
      m_hadInput = false;
    }

    ~VVRWrapper() {
      delete m_lensShader;

      glDeleteBuffers(1, &m_glIDVertBuffer);
      glDeleteBuffers(1, &m_glIDIndexBuffer);

      if (m_unLensVAO != 0) {
        glDeleteVertexArrays(1, &m_unLensVAO);
      }

      vr::VR_Shutdown();
    }

    bool Initialize() {
      // Loading the SteamVR Runtime
      WasGLErrorPlusPrint();
      vr::EVRInitError eError = vr::VRInitError_None;
      vr::IVRSystem* HMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

      if (eError != vr::VRInitError_None)
      {
        printf("Couldn't init openvr: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
        return false;
      }

      m_HMD = HMD;
      return !WasGLErrorPlusPrint();
    }

    //bool SetupTrackedDeviceModels() {
    //  return false;
    //}

    struct VertexDataLens
    {
      Vec2f position;
      Vec2f texCoordRed;
      Vec2f texCoordGreen;
      Vec2f texCoordBlue;
    };
    
    // Pulled nearly directly from openvr sample
    void SetupDistortion()
    {
      if (!m_HMD)
        return;
      WasGLErrorPlusPrint();

      GLushort m_iLensGridSegmentCountH = 43;
      GLushort m_iLensGridSegmentCountV = 43;

      float w = (float)(1.0 / float(m_iLensGridSegmentCountH - 1));
      float h = (float)(1.0 / float(m_iLensGridSegmentCountV - 1));

      float u, v = 0;

      std::vector<VertexDataLens> vVerts(0);
      VertexDataLens vert;

      //left eye distortion verts
      float Xoffset = -1;
      for (int y = 0; y < m_iLensGridSegmentCountV; y++)
      {
        for (int x = 0; x < m_iLensGridSegmentCountH; x++)
        {
          u = x*w; v = 1 - y*h;
          vert.position = Vec2f(Xoffset + u, -1 + 2 * y*h);

          vr::DistortionCoordinates_t dc0 = m_HMD->ComputeDistortion(vr::Eye_Left, u, v);

          vert.texCoordRed = Vec2f(dc0.rfRed[0], 1 - dc0.rfRed[1]);
          vert.texCoordGreen = Vec2f(dc0.rfGreen[0], 1 - dc0.rfGreen[1]);
          vert.texCoordBlue = Vec2f(dc0.rfBlue[0], 1 - dc0.rfBlue[1]);

          vVerts.push_back(vert);
        }
      }

      //right eye distortion verts
      Xoffset = 0;
      for (int y = 0; y < m_iLensGridSegmentCountV; y++)
      {
        for (int x = 0; x < m_iLensGridSegmentCountH; x++)
        {
          u = x*w; v = 1 - y*h;
          vert.position = Vec2f(Xoffset + u, -1 + 2 * y*h);

          vr::DistortionCoordinates_t dc0 = m_HMD->ComputeDistortion(vr::Eye_Right, u, v);

          vert.texCoordRed = Vec2f(dc0.rfRed[0], 1 - dc0.rfRed[1]);
          vert.texCoordGreen = Vec2f(dc0.rfGreen[0], 1 - dc0.rfGreen[1]);
          vert.texCoordBlue = Vec2f(dc0.rfBlue[0], 1 - dc0.rfBlue[1]);

          vVerts.push_back(vert);
        }
      }

      std::vector<GLushort> vIndices;
      GLushort a, b, c, d;

      GLushort offset = 0;
      for (GLushort y = 0; y < m_iLensGridSegmentCountV - 1; y++)
      {
        for (GLushort x = 0; x < m_iLensGridSegmentCountH - 1; x++)
        {
          a = m_iLensGridSegmentCountH*y + x + offset;
          b = m_iLensGridSegmentCountH*y + x + 1 + offset;
          c = (y + 1)*m_iLensGridSegmentCountH + x + 1 + offset;
          d = (y + 1)*m_iLensGridSegmentCountH + x + offset;
          vIndices.push_back(a);
          vIndices.push_back(b);
          vIndices.push_back(c);

          vIndices.push_back(a);
          vIndices.push_back(c);
          vIndices.push_back(d);
        }
      }

      offset = (m_iLensGridSegmentCountH)*(m_iLensGridSegmentCountV);
      for (GLushort y = 0; y < m_iLensGridSegmentCountV - 1; y++)
      {
        for (GLushort x = 0; x < m_iLensGridSegmentCountH - 1; x++)
        {
          a = m_iLensGridSegmentCountH*y + x + offset;
          b = m_iLensGridSegmentCountH*y + x + 1 + offset;
          c = (y + 1)*m_iLensGridSegmentCountH + x + 1 + offset;
          d = (y + 1)*m_iLensGridSegmentCountH + x + offset;
          vIndices.push_back(a);
          vIndices.push_back(b);
          vIndices.push_back(c);

          vIndices.push_back(a);
          vIndices.push_back(c);
          vIndices.push_back(d);
        }
      }
      m_numDistortionIndices = vIndices.size();

      glGenVertexArrays(1, &m_unLensVAO);
      glBindVertexArray(m_unLensVAO);

      glGenBuffers(1, &m_glIDVertBuffer);
      glBindBuffer(GL_ARRAY_BUFFER, m_glIDVertBuffer);
      glBufferData(GL_ARRAY_BUFFER, vVerts.size()*sizeof(VertexDataLens), &vVerts[0], GL_STATIC_DRAW);

      glGenBuffers(1, &m_glIDIndexBuffer);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glIDIndexBuffer);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, vIndices.size()*sizeof(GLushort), &vIndices[0], GL_STATIC_DRAW);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexDataLens), (void *)offsetof(VertexDataLens, position));

      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexDataLens), (void *)offsetof(VertexDataLens, texCoordRed));

      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexDataLens), (void *)offsetof(VertexDataLens, texCoordGreen));

      glEnableVertexAttribArray(3);
      glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(VertexDataLens), (void *)offsetof(VertexDataLens, texCoordBlue));

      glBindVertexArray(0);

      glDisableVertexAttribArray(0);
      glDisableVertexAttribArray(1);
      glDisableVertexAttribArray(2);
      glDisableVertexAttribArray(3);

      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      WasGLErrorPlusPrint();
    }

    bool SetupShaders() {
      delete m_lensShader;
      m_lensShader = g_renderer.LoadShader("OpenVrDistortion");
      if(!m_lensShader) {
        printf("Lens shader failed to load?");
        return false;
      }
      return true;
    }

    void RenderDistortion() {
      if (!m_lensShader) return;

      WasGLErrorPlusPrint();
      glDisable(GL_DEPTH_TEST);
      glViewport(0, 0, m_eyeRenderWidth, m_eyeRenderHeight);

      glBindVertexArray(m_unLensVAO);
      m_lensShader->StartUsing();
      //glUseProgram(m_unLensProgramID);

      //render left lens (first half of index array )
      glBindTexture(GL_TEXTURE_2D, m_eye[vr::Eye_Left].m_resolveTex->GetTextureID());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glDrawElements(GL_TRIANGLES, m_numDistortionIndices / 2, GL_UNSIGNED_SHORT, 0);

      //render right lens (second half of index array )
      glBindTexture(GL_TEXTURE_2D, m_eye[vr::Eye_Right].m_resolveTex->GetTextureID());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glDrawElements(GL_TRIANGLES, m_numDistortionIndices / 2, GL_UNSIGNED_SHORT, (const void *)(m_numDistortionIndices));

      glBindVertexArray(0);
      m_lensShader->StopUsing();
      //glUseProgram(0);
      WasGLErrorPlusPrint();
    }

    virtual bool InitializeWindow(PlatformWindow* pWindow, float pixelScale) {
      m_pWindow = pWindow;
      if (!m_HMD)
        return false;

      vr::EVRInitError peError = vr::VRInitError_None;

      if (!vr::VRCompositor()) {
        printf("Compositor initialization failed\n");
        return false;
      }

      m_HMD->GetRecommendedRenderTargetSize(&m_eyeRenderWidth, &m_eyeRenderHeight);

      const float pixelsPerDisplayPixel = pixelScale; //0.25f; // 1.0f;
      bool createSuccess = true;
      for (int e = 0; e < 2; e++) {
        m_eye[e].m_framebuffer = new Texture();

        m_eye[e].m_framebuffer->CreateFrameBuffer();
        glBindFramebuffer(GL_FRAMEBUFFER, m_eye[e].m_framebuffer->m_framebuffer_id);
        WasGLErrorPlusPrint();

        m_eye[e].m_depthTex = new Texture();
        //createSuccess &= m_eye[e].m_depthTex->CreateDepthTarget(
        createSuccess &= m_eye[e].m_depthTex->CreateRenderBuffers(
            m_eyeRenderWidth, m_eyeRenderHeight);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
            m_eye[e].m_depthTex->GetTextureID());
        WasGLErrorPlusPrint();

        m_eye[e].m_renderTex = new Texture();
        createSuccess &= m_eye[e].m_renderTex->CreateColorTarget(
            m_eyeRenderWidth, m_eyeRenderHeight);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 
            m_eye[e].m_renderTex->GetTextureID(), 0);
        WasGLErrorPlusPrint();

        m_eye[e].m_resolveTex = new Texture();
        createSuccess &= m_eye[e].m_resolveTex->CreateColorTarget(
            m_eyeRenderWidth, m_eyeRenderHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, m_eye[e].m_resolveTex->m_framebuffer_id);
        WasGLErrorPlusPrint();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            m_eye[e].m_resolveTex->GetTextureID(), 0);
        WasGLErrorPlusPrint();

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
          printf("ERROR: framebuffer not ready: %d\n", status);
          return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        WasGLErrorPlusPrint();
      }
      if (!createSuccess) {
        printf("VR Render/depth target creation failed\n");
        return false;
      }

      // initialize hand representations
      SetupDistortion();
      if(!SetupShaders())
        return false;
        
      s_Initialized = true;
      
      return true;
    }

    //vvr is right-handed system
    // +y is up
    // +x is to the right
    // -z is going away from you
    //fd is ???-apendaged system
    void ConvertSteamVRMatrixToPose4(
        const vr::HmdMatrix34_t &vrPose, Pose4f& pose) {
      float vals[] = {
        vrPose.m[0][0], vrPose.m[0][1], vrPose.m[0][2], 0.0,
        vrPose.m[1][0], vrPose.m[1][1], vrPose.m[1][2], 0.0,
        vrPose.m[2][0], vrPose.m[2][1], vrPose.m[2][2], 0.0,
        0.0f, 0.0f, 0.0f, 1.0f
      };
      static float signs[] = {
        -1.0f, -1.0f, -1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f
      };
      for(int val = 0; val < (sizeof(signs) / sizeof(signs[0])); val++) {
        vals[val] = signs[val] * vals[val];
      }
      memcpy(pose.rotation.raw(), vals, sizeof(vals));
      static bool transpose = false;
      if(transpose) {
        pose.rotation = pose.rotation.transpose();
      }

      {
        static float worldScale = 0.0f; //20.0f; // ugh, move this to somewhere in camera
        static Vec4f posSigns(1.0f, 1.0f, 1.0f, 1.0f);
        pose.position.set(vrPose.m[0][3], vrPose.m[1][3], vrPose.m[2][3], 0.0f);
        pose.position *= worldScale;
        pose.position *= posSigns;
      }
    }

    void ConvertSteamVRMatrixToMatrix4(
      const vr::HmdMatrix44_t &vrMat, Mat4f& fdMat) {
      float vals[] = {
        vrMat.m[0][0], vrMat.m[1][0], vrMat.m[2][0], vrMat.m[3][0],
        vrMat.m[0][1], vrMat.m[1][1], vrMat.m[2][1], vrMat.m[3][1],
        vrMat.m[0][2], vrMat.m[1][2], vrMat.m[2][2], vrMat.m[3][2],
        vrMat.m[0][3], vrMat.m[1][3], vrMat.m[2][3], vrMat.m[3][3]
      };
      static float signs[] = {
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f
      };
      static bool transpose = false;
      if(transpose) {
        fdMat = fdMat.transpose();
      }
      memcpy(fdMat.raw(), vals, sizeof(vals));
    }

    virtual void StartFrame() {
      WasGLErrorPlusPrint();
      vr::VRCompositor()->WaitGetPoses(
        m_devicePoses, vr::k_unMaxTrackedDeviceCount, NULL, 0);

      for (int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice) {
        if (m_devicePoses[nDevice].bPoseIsValid) {
          ConvertSteamVRMatrixToPose4(
            m_devicePoses[nDevice].mDeviceToAbsoluteTracking,
            m_device3Poses[nDevice]);
          if (m_rDeviceClassCharCodes[nDevice] == 0) {
            switch (m_HMD->GetTrackedDeviceClass(nDevice)) {
            case vr::TrackedDeviceClass_Controller:        m_rDeviceClassCharCodes[nDevice] = 'C'; break;
            case vr::TrackedDeviceClass_HMD:               m_rDeviceClassCharCodes[nDevice] = 'H'; break;
            case vr::TrackedDeviceClass_Invalid:           m_rDeviceClassCharCodes[nDevice] = 'I'; break;
            case vr::TrackedDeviceClass_Other:             m_rDeviceClassCharCodes[nDevice] = 'O'; break;
            case vr::TrackedDeviceClass_TrackingReference: m_rDeviceClassCharCodes[nDevice] = 'T'; break;
            default:                                       m_rDeviceClassCharCodes[nDevice] = '?'; break;
            }
            m_strCurrentDeviceClasses += m_rDeviceClassCharCodes[nDevice];
          }
        }
      }

      if (m_devicePoses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
        m_hmdPose = m_device3Poses[vr::k_unTrackedDeviceIndex_Hmd].invert();
      }
      WasGLErrorPlusPrint();
    }

    void StartEye(vr::Hmd_Eye eye, Texture** outRenderColor, Texture** outRenderDepth) {
      WasGLErrorPlusPrint();
      // remove this, it's also set externally, but will be nice for debugging that this vr is on
      glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
      WasGLErrorPlusPrint();
      //glBindFramebuffer( GL_FRAMEBUFFER, leftEyeDesc.m_nRenderFramebufferId );
      glBindFramebuffer(GL_FRAMEBUFFER, m_eye[eye].m_framebuffer->m_framebuffer_id);
      WasGLErrorPlusPrint();
      glViewport(0, 0, m_eyeRenderWidth, m_eyeRenderHeight);
      WasGLErrorPlusPrint();

      ////  glBindFramebuffer(GL_FRAMEBUFFER, m_eyeRenderTex[eye]->m_framebuffer_id);
      //glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      //  GL_TEXTURE_2D, m_eyeRenderTex[eye]->m_texture_id, 0);
      //WasGLErrorPlusPrint();
      //glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      //  GL_TEXTURE_2D, m_eyeDepthTex[eye]->m_texture_id, 0);
      //WasGLErrorPlusPrint();

      if (outRenderColor) {
        *outRenderColor = m_eye[eye].m_renderTex;
      }
      if (outRenderDepth) {
        *outRenderDepth = m_eye[eye].m_depthTex;
      }

      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      WasGLErrorPlusPrint();
    }

    void FinishEye(int eye, Texture** outRenderColor, Texture** outRenderDepth) {
      WasGLErrorPlusPrint();
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      WasFramebufferError();
      WasGLErrorPlusPrint();

      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_eye[eye].m_framebuffer->m_framebuffer_id);
      WasFramebufferError();
      WasGLErrorPlusPrint();
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_eye[eye].m_resolveTex->m_framebuffer_id);
      WasFramebufferError();
      WasGLErrorPlusPrint();

      glBlitFramebuffer(0, 0, m_eyeRenderWidth, m_eyeRenderHeight,
        0, 0, m_eyeRenderWidth, m_eyeRenderHeight,
        GL_COLOR_BUFFER_BIT,
        GL_LINEAR);
      WasGLErrorPlusPrint();

      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      WasGLErrorPlusPrint();
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      WasGLErrorPlusPrint();
    }

    //virtual void SetDebugHeadOrientation(const Mat4f* matrix) {
    //  m_debugHeadPose = matrix;
    //}

    Mat4f GetHMDMatrixProjectionEye(vr::Hmd_Eye nEye, Camera* camera) {
      Mat4f fdMat;
      if (!m_HMD)
        return fdMat;

      vr::HmdMatrix44_t vrMat = m_HMD->GetProjectionMatrix(
        nEye, camera->_zNear, camera->_zFar, vr::API_OpenGL);

      ConvertSteamVRMatrixToMatrix4(vrMat, fdMat);

      return fdMat;
    }

    void StoreHMDMatrixPoseEye(vr::Hmd_Eye nEye, Pose4f& outPose)
    {
      if (!m_HMD)
        return;

      vr::HmdMatrix34_t matEye = m_HMD->GetEyeToHeadTransform(nEye);
      ConvertSteamVRMatrixToPose4(matEye, outPose);

      outPose = outPose.invert();
    }

    // The current intuitive approach is to consider the vr pose as being a 3-pose within the 4-space that is defined as the 4-pose of the camera. To convert from the vr 3-pose to the correct new 4-pose, as defined as already aligned with the camera's xyz-pose out of the 4-pose, transform the vr 3-pose filled out to an identity 4-pose for extra params by the identity matrix, then multiply by the room 4-pose to get the 4-pose of the camera.
    // C is camera 4-pose
    // C * w = C.R * w + C.P, where R is rotation, P is position 
    // C3 is the xyz of C
    // R is the room 4-pose
    // both C and R are relative to world origin, but C is derived per frame from R
    // V is the vr 3-pose
    // for 4-position w
    void UpdateCameraRenderMatrix(vr::Hmd_Eye eye, Camera* pCamera) {
      Pose4f matRoomCameraEye;
      // m_hmdPose should already be an identify padded version of the vr device's coordinates in some space
      Pose4f matEyeToHead;
      StoreHMDMatrixPoseEye(eye, matEyeToHead);

      // ok just do from the origin to start
      matRoomCameraEye = matEyeToHead * m_hmdPose;
      //matMVP = m_mat4ProjectionLeft * m_mat4eyePosLeft * m_mat4HMDPose;

      //  const ovrVector3f& localPosOvr = m_eyeDesc[eye].HmdToEyeViewOffset;
      //  Vec4f ovrEyePos(-localPosOvr.x, localPosOvr.y, localPosOvr.z, 0.0f);
      //  ovrEyePos *= worldScale;
      //  ovrEyePos = pCamera->_renderMatrix.transpose().transform(ovrEyePos);
      //  //pCamera->_renderPos = pCamera->_cameraPos + ovrEyePos;
      //  pCamera->UpdateRenderMatrix(&localEye, &ovrEyePos);



      pCamera->UpdateRenderMatrix(&matRoomCameraEye);

      //ovrMatrix4f ovrProj = ovrMatrix4f_Projection(m_HMD->DefaultEyeFov[eye],
      //  0.2f /*zNear*/, 1000.0f /*zFar*/, true /*rightHanded*/);
      pCamera->_zProjectionMatrix = GetHMDMatrixProjectionEye(eye, pCamera);
      
      // TODO: there was quite a lot of code here from before that was involved
      // with getting the transforms right from a previous integration. The new
      // one had similar problems. So, should do transform visualization tools
      // and actually stick with the whole philosophy of the project
    }

    virtual void StartLeftEye(
      Camera* pCamera, Texture** outRenderColor, Texture** outRenderDepth) {
      vr::Hmd_Eye eye = vr::Eye_Left;
      if(m_flippedEyes) {
        eye = vr::Eye_Right;
      }
      StartEye(eye, outRenderColor, outRenderDepth);
      UpdateCameraRenderMatrix(eye, pCamera);
    }

    virtual void FinishLeftEye(
      Camera* pCamera, Texture** outRenderColor, Texture** outRenderDepth) {
      vr::Hmd_Eye eye = vr::Eye_Left;
      if(m_flippedEyes) {
        eye = vr::Eye_Right;
      }
      FinishEye(eye, outRenderColor, outRenderDepth);
    }

    virtual void StartRightEye(
      Camera* pCamera, Texture** outRenderColor, Texture** outRenderDepth) {
      vr::Hmd_Eye eye = vr::Eye_Right;
      if(m_flippedEyes) {
        eye = vr::Eye_Left;
      }
      StartEye(eye, outRenderColor, outRenderDepth);
      UpdateCameraRenderMatrix(eye, pCamera);
    }

    virtual void FinishRightEye(
      Camera* pCamera, Texture** outRenderColor, Texture** outRenderDepth) {
      vr::Hmd_Eye eye = vr::Eye_Right;
      if(m_flippedEyes) {
        eye = vr::Eye_Left;
      }
      FinishEye(eye, outRenderColor, outRenderDepth);
    }

    virtual void FinishFrame() {
      WasGLErrorPlusPrint();

      RenderDistortion();
      WasGLErrorPlusPrint();

      //vr::Texture_t leftEyeTexture = { (void*)(m_eye[vr::Eye_Left].m_resolveTex->GetTextureID()), vr::API_OpenGL, vr::ColorSpace_Linear }; //vr::ColorSpace_Gamma };
      vr::Texture_t leftEyeTexture = { (void*)(m_eye[vr::Eye_Left].m_renderTex->GetTextureID()), vr::API_OpenGL, vr::ColorSpace_Linear }; //vr::ColorSpace_Gamma };
      vr::EVRCompositorError err = vr::VRCompositor()->Submit(vr::Eye_Left, &leftEyeTexture);
      if(err != vr::VRCompositorError_None) {
        printf("openvr compositor (left) error: %d\n", err);
      }
      // the compositor is generating opengl error so clear it I guess?
      GLenum droppedError = glGetError();
      static bool didOnce = false;
      if(!didOnce && droppedError != GL_NO_ERROR) {
        const GLubyte *errString = gluErrorString(droppedError);
        printf("openvr compositor (left) OpenGL Error: %s\n", errString);
        didOnce = true;
      }

      WasGLErrorPlusPrint();
      //vr::Texture_t rightEyeTexture = { (void*)m_eye[vr::Eye_Right].m_resolveTex->GetTextureID(), vr::API_OpenGL, vr::ColorSpace_Linear }; // vr::ColorSpace_Gamma };
      vr::Texture_t rightEyeTexture = { (void*)m_eye[vr::Eye_Right].m_renderTex->GetTextureID(), vr::API_OpenGL, vr::ColorSpace_Linear }; // vr::ColorSpace_Gamma };
      err = vr::VRCompositor()->Submit(vr::Eye_Right, &rightEyeTexture);
      if(err != vr::VRCompositorError_None) {
        printf("openvr compositor (right) error: %d\n", err);
      }
      // the compositor is generating opengl error so clear it I guess?
      GLenum droppedAnotherError = glGetError();
      static bool didAnotherOneOnce = false;
      if(!didAnotherOneOnce && droppedAnotherError != GL_NO_ERROR) {
        const GLubyte *errString = gluErrorString(droppedAnotherError);
        printf("openvr compositor (right) OpenGL Error: %s\n", errString);
        didAnotherOneOnce = true;
      }
      WasGLErrorPlusPrint();

      //  if(m_doScreenSaver) {
      //    float maxDeltaPos = 0.0f;
      //    float maxDeltaRot = 0.0f;
      //    for(int i = 0; i < 2; i++) {

      //      Vec4f oldPos(m_lastEyeRenderPose[i].Position.x, m_lastEyeRenderPose[i].Position.y, m_lastEyeRenderPose[i].Position.z, 0.0f);
      //      Vec4f newPos(m_eyeRenderPose[i].Position.x, m_eyeRenderPose[i].Position.y, m_eyeRenderPose[i].Position.z, 0.0f);
      //      maxDeltaPos = max((oldPos - newPos).length(), maxDeltaPos);

      //      // TODO: do a proper quat slerp dist? This is probably fine actually
      //      Vec4f oldRot(m_lastEyeRenderPose[i].Orientation.x, m_lastEyeRenderPose[i].Orientation.y, m_lastEyeRenderPose[i].Orientation.z, m_lastEyeRenderPose[i].Orientation.w);
      //      Vec4f newRot(m_eyeRenderPose[i].Orientation.x, m_eyeRenderPose[i].Orientation.y, m_eyeRenderPose[i].Orientation.z, m_eyeRenderPose[i].Orientation.w);
      //      maxDeltaRot = max((oldRot - newRot).length(), maxDeltaRot);
      //    }

      //    if(maxDeltaPos > s_screenSaverMoveThreshold || maxDeltaRot > s_screenSaverRotateThreshold) {
      //      m_hadInput = true;
      //    }

      //    //printf("VR input:%d pos:%f rot:%f\n", m_hadInput, maxDeltaPos, maxDeltaRot);

      //  }
      WasGLErrorPlusPrint();
    }

    virtual void SetIsUsingVR(bool usingVR) {
      if (!s_Initialized) return;
      if (s_UsingVR == usingVR) return;

      if (s_UsingVR) {
        s_UsingVR = false;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int restoreWidth;
        int restoreHeight;
        m_pWindow->GetWidthHeight(&restoreWidth, &restoreHeight);
        glViewport(0, 0, restoreWidth, restoreHeight);
      }
      else {
        s_UsingVR = true;
      }
    }

    virtual void SetVRPreferredMovementMode(Camera* pCamera) {
      pCamera->setMovementMode(Camera::MovementMode::ROOM);
    }

    //virtual std::string GetDeviceName() {
    //  if(!m_HMD) return std::string("");
    //  return std::string(m_HMD->DisplayDeviceName);
    //}

    virtual bool GetIsDebugDevice() { return false; }

    virtual bool GetPerEyeRenderSize(int& width, int& height) const {
      width = m_eyeRenderWidth;
      height = m_eyeRenderHeight;
      return true;
    }

    virtual bool GetTotalRenderSize(int& width, int& height) const {
      if(!m_HMD) return false;
      width = m_eyeRenderWidth;
      height = m_eyeRenderHeight * 2; //uh? dunno?
      return true;
    }



    //virtual void Recenter() {
    //  if(!m_HMD) return;
    //  ovrHmd_RecenterPose(m_HMD);
    //}

    ALIGNED_ALLOC_NEW_DEL_OVERRIDE
  };
  

  VRWrapper* VRWrapper::CreateVR() {
    std::unique_ptr<VVRWrapper> vrWrapper(new VVRWrapper);

    if (!vrWrapper->Initialize()) {
      return NULL;
    }

    return vrWrapper.release();
  }
#else //linux_platform
  VRWrapper* VRWrapper::CreateVR() {
    return NULL;
  }
#endif  //defined(WIN32)


}; // namespace fd
#endif //def FD_VR_USE_OPENVR
