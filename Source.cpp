#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <wincodec.h>
#include "DrawingObject.h" 

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

// グローバル変数
CDocument g_document;
ID2D1Factory* g_pD2DFactory = nullptr;
ID2D1HwndRenderTarget* g_pRenderTarget = nullptr;

// 描画中のストローク
std::shared_ptr<CFreehandStroke> g_currentStroke = nullptr;
bool g_isDrawing = false;

// AI補完プレビュー関連のグローバル変数
std::shared_ptr<IDrawableObject> g_pComplementPreview = nullptr; // 補完後のオブジェクト（半透明で表示）
std::shared_ptr<IDrawableObject> g_pOriginalObject = nullptr;    // 補完前のオブジェクト（プレビュー確定時に必要）
size_t g_previewIndex = 0; // 置き換え対象のインデックス

// Direct2Dの初期化
HRESULT CreateD2DResources(HWND hWnd) {
    HRESULT hr = S_OK;
    if (!g_pRenderTarget) {
        RECT rc;
        GetClientRect(hWnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        // レンダリングターゲットの作成
        hr = g_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hWnd, size),
            &g_pRenderTarget
        );
    }
    return hr;
}

// Direct2Dのリソース破棄
void DiscardD2DResources() {
    if (g_pRenderTarget) {
        g_pRenderTarget->Release();
        g_pRenderTarget = nullptr;
    }
}

// ヘルパー関数: プレビューを破棄する
void DiscardPreview() {
    g_pComplementPreview = nullptr;
    g_pOriginalObject = nullptr;
}

// 描画処理
void OnPaint(HWND hWnd) {
    HRESULT hr = CreateD2DResources(hWnd);
    if (SUCCEEDED(hr)) {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);

        g_pRenderTarget->BeginDraw();
        g_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

        // ドキュメント内の全オブジェクトを描画
        g_document.DrawAll(g_pRenderTarget);

        // 現在描画中のストロークを描画
        if (g_currentStroke) {
            g_currentStroke->Draw(g_pRenderTarget);
        }

        // AI補完プレビューの描画 (半透明)
        if (g_pComplementPreview) {
            ID2D1Layer* pLayer = nullptr;
            g_pRenderTarget->CreateLayer(NULL, &pLayer);

            // Direct2D 1.0 の D2D1_LAYER_PARAMETERS を使用 (前回のビルドエラー対策)
            D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters(
                D2D1::RectF(0, 0, g_pRenderTarget->GetSize().width, g_pRenderTarget->GetSize().height),
                NULL,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::Matrix3x2F::Identity(),
                0.5f, // 不透明度 50%
                NULL,
                D2D1_LAYER_OPTIONS_NONE
            );

            g_pRenderTarget->PushLayer(layerParams, pLayer);

            // プレビューを描画
            g_pComplementPreview->Draw(g_pRenderTarget);

            g_pRenderTarget->PopLayer();
            if (pLayer) pLayer->Release();
        }

        hr = g_pRenderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET) {
            DiscardD2DResources();
        }

        EndPaint(hWnd, &ps);
    }
}

// ウィンドウプロシージャ
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory))) {
            return -1;
        }
        return 0;

    case WM_SIZE:
        if (g_pRenderTarget) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            g_pRenderTarget->Resize(D2D1::SizeU(rc.right, rc.bottom));
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        // プレビュー中に描画を開始した場合、プレビューを破棄
        if (g_pComplementPreview) {
            DiscardPreview();
            InvalidateRect(hWnd, NULL, FALSE);
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        D2D1_COLOR_F color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f); // 黒
        float width = 3.0f;
        g_currentStroke = std::make_shared<CFreehandStroke>(color, width);
        g_currentStroke->AddPoint(D2D1::Point2F((float)x, (float)y));
        g_isDrawing = true;
        SetCapture(hWnd);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_isDrawing && g_currentStroke) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            g_currentStroke->AddPoint(D2D1::Point2F((float)x, (float)y));
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_isDrawing && g_currentStroke && g_currentStroke->GetPoints().size() > 1) {
            // 1. オブジェクトをドキュメントのリストに追加 (コマンド記録なし)
            g_document.AddObject(g_currentStroke, false);

            // 2. Addコマンドを作成し、Execute()でインデックスを保存し、スタックに記録
            auto addCommand = std::make_unique<CAddObjectCommand>(&g_document, g_currentStroke);
            addCommand->Execute();
            g_document.RecordCommand(std::move(addCommand));

            // 3. 補完プレビューを試みる
            std::shared_ptr<IDrawableObject> lastObj = g_document.GetLastObject();
            size_t index = g_document.GetLastObjectIndex();

            if (lastObj) {
                std::shared_ptr<CFreehandStroke> stroke =
                    std::dynamic_pointer_cast<CFreehandStroke>(lastObj);

                if (stroke) {
                    stroke->Complement(); // 補完可能かの判定を実行

                    if (stroke->IsComplementable()) {

                        D2D1_COLOR_F previewColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
                        float previewWidth = 3.0f;

                        // 検出された形状に応じてプレビューオブジェクトを生成
                        switch (stroke->m_detectedShape) {
                        case CFreehandStroke::ShapeType::Line: {
                            D2D1_POINT_2F start = stroke->GetPoints().front();
                            D2D1_POINT_2F end = stroke->GetPoints().back();
                            g_pComplementPreview = std::make_shared<CLineSegment>(
                                start, end, previewColor, previewWidth
                            );
                            break;
                        }
                        case CFreehandStroke::ShapeType::Ellipse:
                        case CFreehandStroke::ShapeType::Curve: { // 曲線と楕円は、ここではCEllipseSegmentでプレビュー
                            g_pComplementPreview = std::make_shared<CEllipseSegment>(
                                stroke->m_complementEllipse, previewColor, previewWidth
                            );
                            break;
                        }
                        default:
                            break;
                        }

                        if (g_pComplementPreview) {
                            g_pOriginalObject = lastObj;
                            g_previewIndex = index;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                }
            }

            g_currentStroke = nullptr;
        }
        g_isDrawing = false;
        ReleaseCapture();
        return 0;

    case WM_KEYDOWN:
    {
        // Ctrl+Z (Undo)
        if (wParam == 'Z' && GetKeyState(VK_CONTROL) & 0x8000) {
            DiscardPreview();
            g_document.Undo();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        // Ctrl+Y (Redo)
        else if (wParam == 'Y' && GetKeyState(VK_CONTROL) & 0x8000) {
            DiscardPreview();
            g_document.Redo();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        // Tab (AI補完確定)
        else if (wParam == VK_TAB) {
            if (g_pComplementPreview) {
                // 補完コマンドを作成・実行・記録
                auto complementCommand = std::make_unique<CComplementCommand>(
                    &g_document,
                    g_previewIndex,
                    g_pOriginalObject,
                    g_pComplementPreview
                );

                complementCommand->Execute();
                g_document.RecordCommand(std::move(complementCommand));

                DiscardPreview();
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_PAINT:
        OnPaint(hWnd);
        return 0;

    case WM_DESTROY:
        DiscardD2DResources();
        if (g_pD2DFactory) g_pD2DFactory->Release();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

// Win32 エントリポイント
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"D2D Drawing App";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    HWND hWnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Direct2D AI 補完ペイント",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hWnd == NULL) {
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}