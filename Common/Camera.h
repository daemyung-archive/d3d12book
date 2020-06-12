
//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//   
// Simple first person style camera class that lets the viewer explore the 3D scene.
//   -It keeps track of the camera coordinate system relative to the world space
//    so that the view matrix can be constructed.  
//   -It keeps track of the viewing frustum of the camera so that the projection
//    matrix can be obtained.
//***************************************************************************************

#pragma once

#include "d3dUtil.h"

class Camera
{
public:
    Camera();
    ~Camera();

    // 월드 카메라 위치를 얻거나 설정합니다.
    DirectX::XMVECTOR GetPosition() const;
    DirectX::XMFLOAT3 GetPosition3f() const;
    void SetPosition(float x, float y, float z);
    void SetPosition(const DirectX::XMFLOAT3& v);

    // 카메라 기저 벡터들을 얻습니다.
    DirectX::XMVECTOR GetRight() const;
    DirectX::XMFLOAT3 GetRight3f() const;
    DirectX::XMVECTOR GetUp() const;
    DirectX::XMFLOAT3 GetUp3f() const;
    DirectX::XMVECTOR GetLook() const;
    DirectX::XMFLOAT3 GetLook3f() const;

    // 프러스텀 속성들을 얻습니다.
    float GetNearZ() const;
    float GetFarZ() const;
    float GetAspect() const;
    float GetFovY() const;
    float GetFovX() const;

    // 뷰 스페이스 좌표계에서 가깝거나 먼 평면의 거리를 얻습니다.
    float GetNearWindowWidth() const;
    float GetNearWindowHeight() const;
    float GetFarWindowWidth() const;
    float GetFarWindowHeight() const;

    // 프러스트럼을 설정합니다.
    void SetLens(float fovY, float aspect, float zn, float zf);

    // LookAt 파라미터를 통해 카메라 스페이스를 정의합니다.
    void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
    void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

    // 뷰/프로젝션 메트릭스를 얻습니다.
    DirectX::XMMATRIX GetView() const;
    DirectX::XMMATRIX GetProj() const;

    DirectX::XMFLOAT4X4 GetView4x4f() const;
    DirectX::XMFLOAT4X4 GetProj4x4f() const;

    // 카메라를 거리 d만큼 횡/축 이동합니다.
    void Strafe(float d);
    void Walk(float d);

    // 카메라를 회전시킵니다.
    void Pitch(float angle);
    void RotateY(float angle);

    // 카메라의 위치/방향이 수정된 뒤에 뷰 메트릭스를 다시 계산하기 위해 호출합니다.
    void UpdateViewMatrix();

private:
    // 세계 공간에서의 카메라 좌표계.
    DirectX::XMFLOAT3 mPosition = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 mRight = {1.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 mUp = {0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 mLook = {0.0f, 0.0f, 1.0f};

    // 프러스트럼 속성 캐쉬.
    float mNearZ = 0.0f;
    float mFarZ = 0.0f;
    float mAspect = 0.0f;
    float mFovY = 0.0f;
    float mNearWindowHeight = 0.0f;
    float mFarWindowHeight = 0.0f;

    bool mViewDirty = true;

    // 뷰/프로젝션 메트릭스 캐쉬.
    DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();
};