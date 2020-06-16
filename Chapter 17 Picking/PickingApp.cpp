//***************************************************************************************
// PickingApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//
// Shows how to draw a box in Direct3D 12.
//
// Controls:
//   Hold the left mouse button down and move the mouse to rotate.
//   Hold the right mouse button down and move the mouse to zoom in and out.
//***************************************************************************************

#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "Common/Camera.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct ObjectConstants
{
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    UINT MaterialIndex;
    UINT ObjPad0;
    UINT ObjPad1;
    UINT ObjPad2;
};

struct PassConstants
{
    XMFLOAT4X4 View = MathHelper::Identity4x4();
    XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
    float cbPerObjectPad1 = 0.0f;
    XMFLOAT2 RenderTargetSize = {0.0f, 0.0f};
    XMFLOAT2 InvRenderTargetSize = {0.0f, 0.0f};
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = {0.0f, 0.0f, 0.0f, 1.0f};

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light Lights[MaxLights];
};

struct MaterialData
{
    DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
    float Roughness = 64.0f;

    // 텍스쳐 맵핑에 사용됩니다.
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    UINT DiffuseMapIndex = 0;
    UINT MaterialPad0;
    UINT MaterialPad1;
    UINT MaterialPad2;
};

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC;
};

// CPU에서 한 프레임을 그리기 위한 커맨드들을 기록하기 위한 리소스들을 저장합니다.
struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // GPU가 모든 명령들을 처리하기 전까지 명령 할당자를 리셋할 수 없습니다.
    // 그러므로 매 프레임마다 명령 할당자가 필요합니다.
    ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // GPU가 모든 명령들을 처리하기 전까지 상수 버퍼를 업데이트 할 수 없습니다.
    // 그러므로 매 프레임마다 상수 버퍼가 필요합니다.
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;

    // 펜스 값은 현재 펜스 지점까지의 명령들을 표시합니다.
    // 이 값은 아직 GPU에 의해서 자원들이 사용하는지 검사할 수 있게 해줍니다.
    UINT64 Fence = 0;
};

// 도형을 그리는데 필요한 파라미터들을 저장한 가벼운 구조체입니다.
// 이 구조체는 앱마다 굉장히 다를것 입니다.
struct RenderItem
{
    RenderItem() = default;

    bool Visible = true;

    BoundingBox Bounds;

    // 월드 공간에서 도형의 위치, 회전, 스케일을 정의한 메트릭스입니다.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // 오브젝트의 데이터가 변경됬는지를 나타내는 더티 플레그입니다.
    // 더티 플레그가 활성화되어 있으면 상수 버퍼를 업데이트 해줘야 합니다.
    // 매 프레임 자원마다 오브젝트 상수 버퍼가 존재하기 때문에
    // 오브젝트의 데이터가 변경됬을때 NumFramesDirty = gNumFrameResource로
    // 설정해야합니다. 이렇게 하므로써 모든 프레임 리소스가 업데이트가 됩니다.
    int NumFramesDirty = gNumFrameResources;

    // 렌더 아이템에 해당하는 물체 상수 버퍼의 인덱스 입니다.
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    // 도형 토폴로지입니다.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstance 파라미터들 입니다.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Highlight,
    Count
};

class PickingApp : public D3DApp
{
public:
    PickingApp(HINSTANCE hInstance);
    PickingApp(const PickingApp& rhs) = delete;
    PickingApp& operator=(const PickingApp& rhs) = delete;
    ~PickingApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildCarGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    void Pick(int sx, int sy);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // 렌더 아이템 목록.
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;

    // PSO에 의해 나눠진 렌더 아이템 목록.
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    RenderItem* mPickedRitem = nullptr;

    PassConstants mMainPassCB;

    Camera mCamera;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
                   PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        PickingApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&CmdListAlloc)));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
}

FrameResource::~FrameResource()
{
}

PickingApp::PickingApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

PickingApp::~PickingApp()
{
}

bool PickingApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // 초기화 명령들을 기록하기 위해 커맨드 리스트를 리셋합니다.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCamera.LookAt(
        XMFLOAT3(5.0f, 4.0f, -15.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f));

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildCarGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // 초기화 명령들을 실행시킵니다.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    // 초기화가 종료될 때가지 기다립니다.
    FlushCommandQueue();

    return true;
}

void PickingApp::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void PickingApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    // 다음 프레임 리소스의 자원을 얻기위해 순환합니다.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // 현재 프레임 리소스에 대한 명령들이 GPU에서 처리 되었습니까?
    // 처리되지 않았다면 커맨드들의 펜스 지점까지 GPU가 처리할 때까지 기다려야합니다.
    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
}

void PickingApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // 커맨드 기록을 위한 메모리를 재활용 합니다.
    // 제출한 커맨드들이 GPU에서 모두 끝났을때 리셋할 수 있습니다.
    ThrowIfFailed(cmdListAlloc->Reset());

    // ExecuteCommandList를 통해 커맨드 큐에 제출한 다음에 커맨드 리스트를 리셋할 수 있습니다.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // 리소스의 상태를 렌더링을 할 수 있도록 변경합니다.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                           D3D12_RESOURCE_STATE_PRESENT,
                                                                           D3D12_RESOURCE_STATE_RENDER_TARGET));

    // 백 버퍼와 뎁스 버퍼를 클리어 합니다.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // 어디에 렌더링을 할지 설정합니다.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = {mSrvDescriptorHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    // 장면에서 사용되는 모든 메터리얼을 바인드 합니다.
    // 스트럭쳐 버퍼는 힙을 바로 루트 디스크립터로 설정할 수 있습니다.
    auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, matBuffer->GetGPUVirtualAddress());

    // 장면에서 사용되는 모든 텍스쳐를 바인드 합니다.
    // 여기서 디스크립터의 첫번째를 테이블에 설정합니다.
    // 루트 시그네쳐가 테이블에서 몇개의 디스크립터가 필요한지 알고있습니다.
    mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    mCommandList->SetPipelineState(mPSOs["highlight"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Highlight]);

    // 리소스의 상태를 출력할 수 있도록 변경합니다.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                           D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                           D3D12_RESOURCE_STATE_PRESENT));

    // 커맨드 기록을 종료합니다.
    ThrowIfFailed(mCommandList->Close());

    // 커맨드 리스트의 실행을 위해 큐에 제출합니다.
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    // 백 버퍼와 프론트 버퍼를 교체합니다.
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // 이 펜스 지점까지 커맨드들을 표시하기 위해 펜스 값을 증가합니다,
    mCurrFrameResource->Fence = ++mCurrentFence;

    // 새 펜스 지점을 설정하는 인스트럭션을 커맨드 큐에 추가합니다.
    // 어플리케이션은 GPU 시간축에 있지 않기 때문에,
    // GPU가 모든 커맨드들의 처리가 완료되기 전까지 Signal()을 처리하지 않습니다.
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void PickingApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        mLastMousePos.x = x;
        mLastMousePos.y = y;

        SetCapture(mhMainWnd);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        Pick(x, y);
    }
}

void PickingApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void PickingApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        // 마우스 한 픽셀의 이동을 0.25도에 대응시킵니다.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void PickingApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(10.0f * dt);

    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-10.0f * dt);

    mCamera.UpdateViewMatrix();
}

void PickingApp::AnimateMaterials(const GameTimer& gt)
{
}

void PickingApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        // 상수들이 바뀌었 때만 상수 버퍼 데이터를 업데이트 합니다.
        // 이것은 매 프레임 자원마다 수행해야 합니다.
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            objConstants.MaterialIndex = e->Mat->MatCBIndex;

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // 다음 프레임 리소스도 마찬가지로 업데이트 되어야 합니다.
            e->NumFramesDirty--;
        }
    }
}

void PickingApp::UpdateMaterialBuffer(const GameTimer& gt)
{
    auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
    for (auto& e : mMaterials)
    {
        // 상수 데이터가 변경되었을 때만 상수 버퍼를 업데이트합니다.
        // 상수 데이터가 변경되었으면 모든 프레임 리소스가 업데이트 되어야 합니다.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            // 다음 프레임도 업데이트 되어야 합니다.
            mat->NumFramesDirty--;
        }
    }
}

void PickingApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = {0.25f, 0.25f, 0.35f, 1.0f};
    mMainPassCB.Lights[0].Direction = {0.57735f, -0.57735f, 0.57735f};
    mMainPassCB.Lights[0].Strength = {0.8f, 0.8f, 0.8f};
    mMainPassCB.Lights[1].Direction = {-0.57735f, -0.57735f, 0.57735f};
    mMainPassCB.Lights[1].Strength = {0.4f, 0.4f, 0.4f};
    mMainPassCB.Lights[2].Direction = {0.0f, -0.707f, -0.707f};
    mMainPassCB.Lights[2].Strength = {0.2f, 0.2f, 0.2f};

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void PickingApp::LoadTextures()
{
    auto defaultDiffuseTex = std::make_unique<Texture>();
    defaultDiffuseTex->Name = "defaultDiffuseTex";
    defaultDiffuseTex->Filename = L"..\\Textures\\white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
                                                      mCommandList.Get(), defaultDiffuseTex->Filename.c_str(),
                                                      defaultDiffuseTex->Resource, defaultDiffuseTex->UploadHeap));

    mTextures[defaultDiffuseTex->Name] = std::move(defaultDiffuseTex);
}

void PickingApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    // 루트 파라미터는 테이블, 루트 디스크립터, 루트 상수가 될 수 있습니다.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // 루트 CBV를 생성합니다.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 1);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = GetStaticSamplers();

    // 루트 시그네쳐는 루트 파라미터 배열입니다.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
                                            (UINT)staticSamplers.size(), staticSamplers.data(),
                                            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // 한 개의 상수 버퍼로 구성된 디스크립터 레인지를 가르키고 있는
    // 두 개의 슬롯으로 구성되어있는 루트 시그네쳐를 생성합니다.
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void PickingApp::BuildDescriptorHeaps()
{
    //
    // SRV 힙을 생성합니다.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    //
    // 디스크립터로 힙에 내용을 채웁니다.
    //
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto defaultDiffuseTex = mTextures["defaultDiffuseTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = defaultDiffuseTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = defaultDiffuseTex->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    md3dDevice->CreateShaderResourceView(defaultDiffuseTex.Get(), &srvDesc, hDescriptor);
}

void PickingApp::BuildShadersAndInputLayout()
{
    const D3D_SHADER_MACRO alphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        NULL, NULL
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
}

void PickingApp::BuildCarGeometry()
{
    std::ifstream fin("Models/car.txt");

    if (!fin)
    {
        MessageBox(0, L"Models/car.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;
    fin >> ignore >> tcount;
    fin >> ignore >> ignore >> ignore >> ignore;

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

        XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

        vertices[i].TexC = {0.0f, 0.0f};

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    for (UINT i = 0; i < tcount; ++i)
    {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "carGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
                                                        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
                                                       mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    submesh.Bounds = bounds;

    geo->DrawArgs["car"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void PickingApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    //
    // 불투명 오브젝트를 위한 PSO 생성.
    //
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = {mInputLayout.data(), (UINT)mInputLayout.size()};
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    //
    // 강조 오브젝트를 위한 PSO 생성.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC highlightPsoDesc = opaquePsoDesc;

    // 뎁스 테스트를 <에서 <=으로 바꾸고 나서 삼각형을 두번 그리더라도 뎁스 테스트를 성공합니다.
    highlightPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // 표준 투명 블렌딩입니다.
    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;
    transparencyBlendDesc.LogicOpEnable = false;
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    highlightPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&highlightPsoDesc, IID_PPV_ARGS(&mPSOs["highlight"])));
}

void PickingApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 2, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void PickingApp::BuildMaterials()
{
    auto gray0 = std::make_unique<Material>();
    gray0->Name = "gray0";
    gray0->MatCBIndex = 0;
    gray0->DiffuseSrvHeapIndex = 0;
    gray0->DiffuseAlbedo = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
    gray0->FresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
    gray0->Roughness = 0.0f;

    auto highlight0 = std::make_unique<Material>();
    highlight0->Name = "highlight0";
    highlight0->MatCBIndex = 1;
    highlight0->DiffuseSrvHeapIndex = 0;
    highlight0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.6f);
    highlight0->FresnelR0 = XMFLOAT3(0.06f, 0.06f, 0.06f);
    highlight0->Roughness = 0.0f;


    mMaterials["gray0"] = std::move(gray0);
    mMaterials["highlight0"] = std::move(highlight0);
}

void PickingApp::BuildRenderItems()
{
    auto carRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&carRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    XMStoreFloat4x4(&carRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    carRitem->ObjCBIndex = 0;
    carRitem->Mat = mMaterials["gray0"].get();
    carRitem->Geo = mGeometries["carGeo"].get();
    carRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    carRitem->Bounds = carRitem->Geo->DrawArgs["car"].Bounds;
    carRitem->IndexCount = carRitem->Geo->DrawArgs["car"].IndexCount;
    carRitem->StartIndexLocation = carRitem->Geo->DrawArgs["car"].StartIndexLocation;
    carRitem->BaseVertexLocation = carRitem->Geo->DrawArgs["car"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(carRitem.get());

    auto pickedRitem = std::make_unique<RenderItem>();
    pickedRitem->World = MathHelper::Identity4x4();
    pickedRitem->TexTransform = MathHelper::Identity4x4();
    pickedRitem->ObjCBIndex = 1;
    pickedRitem->Mat = mMaterials["highlight0"].get();
    pickedRitem->Geo = mGeometries["carGeo"].get();
    pickedRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // 선택될 때까지 선택된 삼각형은 보이지 않습니다.
    pickedRitem->Visible = false;

    // 드로우 콜 파라미터들은 삼각형이 선택되었을 때 채워집니다.
    pickedRitem->IndexCount = 0;
    pickedRitem->StartIndexLocation = 0;
    pickedRitem->BaseVertexLocation = 0;
    mPickedRitem = pickedRitem.get();
    mRitemLayer[(int)RenderLayer::Highlight].push_back(pickedRitem.get());

    mAllRitems.push_back(std::move(carRitem));
    mAllRitems.push_back(std::move(pickedRitem));
}

void PickingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    // 각 렌더 항목에 대해서...
    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void PickingApp::Pick(int sx, int sy)
{
    XMFLOAT4X4 P = mCamera.GetProj4x4f();

    // 뷰 스페이스에서의 선택 레이를 계산합니다.
    float vx = (+2.0f * sx / mClientWidth  - 1.0f) / P(0, 0);
    float vy = (-2.0f * sy / mClientHeight + 1.0f) / P(1, 1);

    // 뷰 스페이스에서 레이를 정의합니다.
    XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

    XMMATRIX V = mCamera.GetView();
    XMMATRIX invV = XMMatrixInverse(&XMMatrixDeterminant(V), V);

    // 처음에 선택된 렌더 아이템이 없다고 가정합니다.
    mPickedRitem->Visible = false;

    // 불투명 렌더 아이템이 선택됬는지 검사합니다.
    for (auto ri : mRitemLayer[(int)RenderLayer::Opaque])
    {
        auto geo = ri->Geo;

        // 보이지 않는 렌더 아이템들은 건너뜁니다.
        if (ri->Visible == false)
            continue;

        XMMATRIX W = XMLoadFloat4x4(&ri->World);
        XMMATRIX invW = XMMatrixInverse(&XMMatrixDeterminant(W), W);

        // 레이를 뷰 스페이스에서 로컬 스페이스로 이동시키는 메트릭스를 계산합니다.
        XMMATRIX toLocal = XMMatrixMultiply(invV, invW);

        rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
        rayDir = XMVector3TransformNormal(rayDir, toLocal);

        // 교차 테스트를 위해 레이 방향을 노말라이즈 합니다.
        rayDir = XMVector3Normalize(rayDir);

        // 먼저 메쉬의 바운딩 박스에 교차 검사를 합니다. 바운딩 박스가 교차되면 메쉬 삼각형과 교차 검사를 합니다.
        // 레이가 바운딩 박스에 교차되지 않으면 메쉬와 레이가 교차되는것은 불가능합니다.
        float tmin = 0.0f;
        if (ri->Bounds.Intersects(rayOrigin, rayDir, tmin))
        {
            // 주의: 데모에서는 버텍스와 인덱스가 어떤 포멧인지 암시적으로 알지만
            // 실제 어플리케이션에선 어떤 포멧으로 캐스팅해야하는지 메타데이터에 저장되어 있어야 합니다.
            auto vertices = (Vertex*)geo->VertexBufferCPU->GetBufferPointer();
            auto indices = (std::uint32_t*)geo->IndexBufferCPU->GetBufferPointer();
            UINT triCount = ri->IndexCount / 3;

            // 카메라에서 가장 가까운 레이와 교차하는 삼각형을 찾습니다.
            tmin = MathHelper::Infinity;
            for (UINT i = 0; i < triCount; ++i)
            {
                // 삼각형을 위한 인덱스.
                UINT i0 = indices[i * 3 + 0];
                UINT i1 = indices[i * 3 + 1];
                UINT i2 = indices[i * 3 + 2];

                // 삼각형을 위한 버텍스 포지션.
                XMVECTOR v0 = XMLoadFloat3(&vertices[i0].Pos);
                XMVECTOR v1 = XMLoadFloat3(&vertices[i1].Pos);
                XMVECTOR v2 = XMLoadFloat3(&vertices[i2].Pos);

                // 가장 가까운 삼각형을 찾기 위해서는 모두 검사해야합니다.
                float t = 0.0f;
                if (TriangleTests::Intersects(rayOrigin, rayDir, v0, v1, v2, t))
                {
                    if (t < tmin)
                    {
                        // 가장 가까운 삼각형을 찾았습니다.
                        tmin = t;
                        UINT pickedTriangle = i;

                        mPickedRitem->Visible = true;
                        mPickedRitem->IndexCount = 3;
                        mPickedRitem->BaseVertexLocation = 0;
                        mPickedRitem->World = ri->World;
                        mPickedRitem->NumFramesDirty = gNumFrameResources;
                        mPickedRitem->StartIndexLocation = 3 * pickedTriangle;
                    }
                }
            }
        }

    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> PickingApp::GetStaticSamplers()
{
    // 어플리케이션은 보통 몇개의 샘플러만 필요합니다. 그래서 자주 사용되는 샘플러 몇개를 정의하고
    // 루트 시그네쳐의 일부분으로 유지합니다.

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp};
}
