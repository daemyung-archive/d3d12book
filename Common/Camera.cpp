
//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
    SetLens(0.25f * MathHelper::Pi, 1.0, 1.0, 1000.0f);
}

Camera::~Camera()
{
}

DirectX::XMVECTOR Camera::GetPosition() const
{
    return XMLoadFloat3(&mPosition);
}

DirectX::XMFLOAT3 Camera::GetPosition3f() const
{
    return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
    mPosition = XMFLOAT3(x, y, z);
    mViewDirty = true;
}

void Camera::SetPosition(const DirectX::XMFLOAT3& v)
{
    mPosition = v;
    mViewDirty = true;
}

DirectX::XMVECTOR Camera::GetRight() const
{
    return XMLoadFloat3(&mRight);
}

DirectX::XMFLOAT3 Camera::GetRight3f() const
{
    return mRight;
}

DirectX::XMVECTOR Camera::GetUp() const
{
    return XMLoadFloat3(&mUp);
}

DirectX::XMFLOAT3 Camera::GetUp3f() const
{
    return mUp;
}

DirectX::XMVECTOR Camera::GetLook() const
{
    return XMLoadFloat3(&mLook);
}

DirectX::XMFLOAT3 Camera::GetLook3f() const
{
    return mLook;
}

float Camera::GetNearZ() const
{
    return mNearZ;
}

float Camera::GetFarZ() const
{
    return mFarZ;
}

float Camera::GetAspect() const
{
    return mAspect;
}

float Camera::GetFovY() const
{
    return mFovY;
}

float Camera::GetFovX() const
{
    float halfWidth = 0.5f * GetNearWindowHeight();
    return 2.0f * atan(halfWidth / mNearZ);
}

float Camera::GetNearWindowWidth() const
{
    return mAspect * mNearWindowHeight;
}

float Camera::GetNearWindowHeight() const
{
    return mNearWindowHeight;
}

float Camera::GetFarWindowWidth() const
{
    return mAspect * mFarWindowHeight;
}

float Camera::GetFarWindowHeight() const
{
    return mFarWindowHeight;
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
    // 프로퍼티들을 캐쉬합니다.
    mFovY = fovY;
    mAspect = aspect;
    mNearZ = zn;
    mFarZ = zf;

    mNearWindowHeight = 2.0f * mNearZ * tanf(0.5f * mFovY);
    mFarWindowHeight  = 2.0f * mFarZ * tanf(0.5f * mFovY);

    XMMATRIX P = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
    XMStoreFloat4x4(&mProj, P);
}

void Camera::LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp)
{
    XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
    XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
    XMVECTOR U = XMVector3Cross(L, R);

    XMStoreFloat3(&mPosition, pos);
    XMStoreFloat3(&mLook, L);
    XMStoreFloat3(&mRight, R);
    XMStoreFloat3(&mUp, U);

    mViewDirty = true;
}

void Camera::LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up)
{
    XMVECTOR P = XMLoadFloat3(&pos);
    XMVECTOR T = XMLoadFloat3(&target);
    XMVECTOR U = XMLoadFloat3(&up);

    LookAt(P, T, U);

    mViewDirty = true;
}

DirectX::XMMATRIX Camera::GetView() const
{
    return XMLoadFloat4x4(&mView);
}

DirectX::XMMATRIX Camera::GetProj() const
{
    return XMLoadFloat4x4(&mProj);
}

DirectX::XMFLOAT4X4 Camera::GetView4x4f() const
{
    return mView;
}

DirectX::XMFLOAT4X4 Camera::GetProj4x4f() const
{
    return mProj;
}

void Camera::Strafe(float d)
{
    // mPosition += d * mRight
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR r = XMLoadFloat3(&mRight);
    XMVECTOR p = XMLoadFloat3(&mPosition);
    XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, r, p));

    mViewDirty = true;
}

void Camera::Walk(float d)
{
    // mPosition += d * mLook
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR l = XMLoadFloat3(&mLook);
    XMVECTOR p = XMLoadFloat3(&mPosition);
    XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, l, p));

    mViewDirty = true;
}

void Camera::Pitch(float angle)
{
    // 상향과 시선 벡터를 오른쪽 벡터에 대해 회전합니다.

    XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);

    XMStoreFloat3(&mUp,   XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
    XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

    mViewDirty = true;
}

void Camera::RotateY(float angle)
{
    // 기저 벡터들을 월드 Y축에 대해 회전합니다.

    XMMATRIX R = XMMatrixRotationY(angle);

    XMStoreFloat3(&mRight, XMVector3TransformNormal(XMLoadFloat3(&mRight), R));
    XMStoreFloat3(&mUp,    XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
    XMStoreFloat3(&mLook,  XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

    mViewDirty = true;
}

void Camera::UpdateViewMatrix()
{
    if (mViewDirty)
    {
        XMVECTOR R = XMLoadFloat3(&mRight);
        XMVECTOR U = XMLoadFloat3(&mUp);
        XMVECTOR L = XMLoadFloat3(&mLook);
        XMVECTOR P = XMLoadFloat3(&mPosition);

        // 카메라의 축들이 서로 직교가 되고 단위 벡터가 되도록 유지합니다.
        L = XMVector3Normalize(L);
        U = XMVector3Normalize(XMVector3Cross(L, R));

        // U, L이 이미 직교 노말 벡터이므로 외적만 하면 됩니다.
        R = XMVector3Cross(U, L);

        // 뷰 메트릭스를 정의합니다.
        float x = -XMVectorGetX(XMVector3Dot(P, R));
        float y = -XMVectorGetX(XMVector3Dot(P, U));
        float z = -XMVectorGetX(XMVector3Dot(P, L));

        XMStoreFloat3(&mRight, R);
        XMStoreFloat3(&mUp, U);
        XMStoreFloat3(&mLook, L);
        
        mView(0, 0) = mRight.x;
        mView(1, 0) = mRight.y;
        mView(2, 0) = mRight.z;
        mView(3, 0) = x;

        mView(0, 1) = mUp.x;
        mView(1, 1) = mUp.y;
        mView(2, 1) = mUp.z;
        mView(3, 1) = y;

        mView(0, 2) = mLook.x;
        mView(1, 2) = mLook.y;
        mView(2, 2) = mLook.z;
        mView(3, 2) = z;

        mView(0, 3) = 0.0f;
        mView(1, 3) = 0.0f;
        mView(2, 3) = 0.0f;
        mView(3, 3) = 1.0f;

        mViewDirty = false;
    }
}
