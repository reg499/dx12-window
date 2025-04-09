#include <windows.h>
#include "d3d12.h"
#include <dxgi1_6.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <iostream>

struct CD3DX12_CPU_DESCRIPTOR_HANDLE : public D3D12_CPU_DESCRIPTOR_HANDLE
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE() = default;
    explicit CD3DX12_CPU_DESCRIPTOR_HANDLE(const D3D12_CPU_DESCRIPTOR_HANDLE& o) :
        D3D12_CPU_DESCRIPTOR_HANDLE(o)
    {
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE(CD3DX12_CPU_DESCRIPTOR_HANDLE const& o) :
        D3D12_CPU_DESCRIPTOR_HANDLE(o)
    {
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE(CD3DX12_CPU_DESCRIPTOR_HANDLE&& o) :
        D3D12_CPU_DESCRIPTOR_HANDLE(o)
    {
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE& operator=(CD3DX12_CPU_DESCRIPTOR_HANDLE const& o)
    {
        ptr = o.ptr;
        return *this;
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE& operator=(CD3DX12_CPU_DESCRIPTOR_HANDLE&& o)
    {
        ptr = o.ptr;
        return *this;
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE(
        D3D12_CPU_DESCRIPTOR_HANDLE base,
        INT offsetScaledByIncrementSize)
    {
        InitOffsetted(base, offsetScaledByIncrementSize);
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE(
        D3D12_CPU_DESCRIPTOR_HANDLE base,
        INT offsetInDescriptors,
        UINT descriptorIncrementSize)
    {
        InitOffsetted(base, offsetInDescriptors, descriptorIncrementSize);
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(INT offsetInDescriptors, UINT descriptorIncrementSize)
    {
        ptr += INT64(offsetInDescriptors) * UINT64(descriptorIncrementSize);
        return *this;
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(INT offsetScaledByIncrementSize)
    {
        ptr += offsetScaledByIncrementSize;
        return *this;
    }
    void InitOffsetted(D3D12_CPU_DESCRIPTOR_HANDLE base, INT offsetScaledByIncrementSize)
    {
        ptr = base.ptr + offsetScaledByIncrementSize;
    }
    void InitOffsetted(
        D3D12_CPU_DESCRIPTOR_HANDLE base,
        INT offsetInDescriptors,
        UINT descriptorIncrementSize)
    {
        ptr = base.ptr + INT64(offsetInDescriptors) * UINT64(descriptorIncrementSize);
    }
};

using Microsoft::WRL::ComPtr;

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

const std::wstring WINDOW_TITLE = L"DirectX 12 Penceresi";

const UINT BUFFER_COUNT = 2;

HWND g_hwnd = nullptr;
ComPtr<ID3D12Device> g_device;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
UINT g_rtvDescriptorSize = 0;
std::vector<ComPtr<ID3D12Resource>> g_renderTargets;
ComPtr<ID3D12CommandAllocator> g_commandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12Fence> g_fence;
UINT64 g_fenceValue = 0;
HANDLE g_fenceEvent = nullptr;
UINT g_frameIndex = 0;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void InitWindow(HINSTANCE hInstance);
void InitDirectX();
void WaitForPreviousFrame();
void Render();
void Cleanup();

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    if (FAILED(CoInitialize(nullptr)))
    {
        MessageBox(NULL, L"COM başlatılamadı!", L"Hata", MB_OK);
        return 1;
    }

    InitWindow(hInstance);

    try
    {
        InitDirectX();

        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                Render();
            }
        }
    }
    catch (std::exception& e)
    {
        MessageBoxA(NULL, e.what(), "Hata", MB_OK);
    }

    Cleanup();
    CoUninitialize();

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
            return 0;
        }
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void InitWindow(HINSTANCE hInstance)
{
    WNDCLASSEX windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"DX12WindowClass";
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindow(
        windowClass.lpszClassName,
        WINDOW_TITLE.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!g_hwnd)
    {
        throw std::runtime_error("Pencere oluşturulamadı!");
    }

    ShowWindow(g_hwnd, SW_SHOW);
}

void InitDirectX()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
    {
        throw std::runtime_error("DXGI Factory oluşturulamadı!");
    }

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIndex = 0; SUCCEEDED(factory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        {
            hardwareAdapter = adapter;
            break;
        }
    }

    if (!hardwareAdapter)
    {
        throw std::runtime_error("Uygun donanım adaptörü bulunamadı!");
    }

    if (FAILED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device))))
    {
        throw std::runtime_error("D3D12 Device oluşturulamadı!");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    if (FAILED(g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue))))
    {
        throw std::runtime_error("Komut kuyruğu oluşturulamadı!");
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = BUFFER_COUNT;
    swapChainDesc.Width = WINDOW_WIDTH;
    swapChainDesc.Height = WINDOW_HEIGHT;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(factory->CreateSwapChainForHwnd(
        g_commandQueue.Get(),
        g_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain)))
    {
        throw std::runtime_error("Swap chain oluşturulamadı!");
    }

    if (FAILED(factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER)))
    {
        throw std::runtime_error("MakeWindowAssociation başarısız oldu!");
    }

    if (FAILED(swapChain.As(&g_swapChain)))
    {
        throw std::runtime_error("SwapChain3 arayüzüne dönüştürülemedi!");
    }

    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = BUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap))))
    {
        throw std::runtime_error("RTV tanımlayıcı yığını oluşturulamadı!");
    }

    g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    g_renderTargets.resize(BUFFER_COUNT);

    for (UINT i = 0; i < BUFFER_COUNT; i++)
    {
        if (FAILED(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]))))
        {
            throw std::runtime_error("Render hedefi oluşturulamadı!");
        }

        g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(g_rtvDescriptorSize);
    }

    if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator))))
    {
        throw std::runtime_error("Komut allocator oluşturulamadı!");
    }

    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_commandList))))
    {
        throw std::runtime_error("Komut listesi oluşturulamadı!");
    }

    g_commandList->Close();

    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
    {
        throw std::runtime_error("Fence oluşturulamadı!");
    }

    g_fenceValue = 1;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
    {
        throw std::runtime_error("Fence olayı oluşturulamadı!");
    }
}

void WaitForPreviousFrame()
{
    const UINT64 fence = g_fenceValue;
    g_commandQueue->Signal(g_fence.Get(), fence);
    g_fenceValue++;

    if (g_fence->GetCompletedValue() < fence)
    {
        g_fence->SetEventOnCompletion(fence, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, INFINITE);
    }

    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

void Render()
{
    g_commandAllocator->Reset();
    g_commandList->Reset(g_commandAllocator.Get(), nullptr);

    // source : present -> render target
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_renderTargets[g_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);

    // Render target 
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_rtvHeap->GetCPUDescriptorHandleForHeapStart(), g_frameIndex, g_rtvDescriptorSize);
    g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // clearColor
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // render target -> present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_commandList->ResourceBarrier(1, &barrier);

    g_commandList->Close();
    ID3D12CommandList* commandLists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    g_swapChain->Present(1, 0);

    WaitForPreviousFrame();
}

void Cleanup()
{
    if (g_fenceEvent)
    {
        CloseHandle(g_fenceEvent);
        g_fenceEvent = nullptr;
    }

    g_renderTargets.clear();
}
