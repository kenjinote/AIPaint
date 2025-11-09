#include "DrawingObject.h"

// === ヘルパー関数: 楕円フィッティング (簡略版) ===
bool FitEllipse(const std::vector<D2D1_POINT_2F>& points, float tolerance, D2D1_ELLIPSE& outEllipse) {
    if (points.size() < 5) return false;

    // 1. バウンディングボックスの計算
    float minX = points[0].x, minY = points[0].y;
    float maxX = points[0].x, maxY = points[0].y;
    for (const auto& p : points) {
        minX = min(minX, p.x);
        maxX = max(maxX, p.x);
        minY = min(minY, p.y);
        maxY = max(maxY, p.y);
    }

    // 2. 楕円のパラメータを設定 (バウンディングボックスの中心と半径から概算)
    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;
    float radiusX = (maxX - minX) / 2.0f;
    float radiusY = (maxY - minY) / 2.0f;

    // 楕円が小さすぎる場合は無視
    if (radiusX < 10.0f || radiusY < 10.0f) return false;

    outEllipse.point = D2D1::Point2F(centerX, centerY);
    outEllipse.radiusX = radiusX;
    outEllipse.radiusY = radiusY;

    // 3. 適合度の判定
    float maxDeviationSq = 0.0f;

    for (const auto& p : points) {
        // 正規化された座標
        float nx = (p.x - centerX) / radiusX;
        float ny = (p.y - centerY) / radiusY;

        // 楕円の式: (x/a)^2 + (y/b)^2 = 1。偏差が0に近いほど適合
        float deviation = std::abs(nx * nx + ny * ny - 1.0f);
        maxDeviationSq = max(maxDeviationSq, deviation);
    }

    // 閾値 0.2 は調整可能。ストロークが閉じていなくても判定が通る可能性あり。
    return maxDeviationSq < 0.2f;
}
// ===================================


// --- CFreehandStroke 実装 ---

CFreehandStroke::CFreehandStroke(D2D1_COLOR_F color, float width)
    : m_color(color), m_strokeWidth(width), m_isComplemented(false) {
}

void CFreehandStroke::AddPoint(D2D1_POINT_2F p) {
    m_points.push_back(p);
}

void CFreehandStroke::Draw(ID2D1RenderTarget* pRT) const {
    if (m_points.size() < 2) return;

    ID2D1SolidColorBrush* pBrush = nullptr;
    pRT->CreateSolidColorBrush(m_color, &pBrush);

    for (size_t i = 0; i < m_points.size() - 1; ++i) {
        pRT->DrawLine(m_points[i], m_points[i + 1], pBrush, m_strokeWidth);
    }

    if (pBrush) pBrush->Release();
}

std::shared_ptr<IDrawableObject> CFreehandStroke::Clone() const {
    auto clone = std::make_shared<CFreehandStroke>(m_color, m_strokeWidth);
    clone->m_points = m_points;
    clone->m_isComplemented = m_isComplemented;
    clone->m_detectedShape = m_detectedShape;
    clone->m_complementEllipse = m_complementEllipse;
    return clone;
}

void CFreehandStroke::Complement() {
    if (m_points.size() < 2) return;

    m_isComplemented = false;
    m_detectedShape = ShapeType::None;

    // --- 1. 直線判定 ---
    D2D1_POINT_2F start = m_points.front();
    D2D1_POINT_2F end = m_points.back();
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float L_sq = dx * dx + dy * dy;
    float L = std::sqrt(L_sq);
    const float LINE_TOLERANCE = m_strokeWidth * 2.0f;
    float maxLineDeviation = 0.0f;

    for (const auto& p : m_points) {
        float A = end.y - start.y;
        float B = start.x - end.x;
        float C = end.x * start.y - end.y * start.x;
        float distance = std::abs(A * p.x + B * p.y + C) / (L > 0.0f ? L : 1.0f);
        maxLineDeviation = max(maxLineDeviation, distance);
    }

    // --- 2. 円/楕円判定 ---
    D2D1_ELLIPSE potentialEllipse = { 0 };
    const float ELLIPSE_FIT_TOLERANCE = 10.0f;
    bool isEllipse = FitEllipse(m_points, ELLIPSE_FIT_TOLERANCE, potentialEllipse);

    // --- 3. 判定結果の適用 ---

    if (isEllipse && maxLineDeviation > 5.0f * LINE_TOLERANCE) {
        // 楕円として適合し、直線として適合しない場合
        m_isComplemented = true;
        m_detectedShape = ShapeType::Ellipse;
        m_complementEllipse = potentialEllipse;

    }
    else if (maxLineDeviation < LINE_TOLERANCE) {
        // 直線として適合する場合 (最も単純なので優先)
        m_isComplemented = true;
        m_detectedShape = ShapeType::Line;

    }
    else if (m_points.size() > 10) {
        // その他、複雑な曲線として認識 (ここではCubic Bézierに補完可能と見なす)
        m_isComplemented = true;
        m_detectedShape = ShapeType::Curve;
    }
}

bool CFreehandStroke::IsComplementable() const {
    // 補完可能と判定され、かつまだ補完されていない状態
    return m_isComplemented && m_detectedShape != ShapeType::None;
}

// --- CLineSegment 実装 ---

CLineSegment::CLineSegment(D2D1_POINT_2F start, D2D1_POINT_2F end, D2D1_COLOR_F color, float width)
    : m_start(start), m_end(end), m_color(color), m_strokeWidth(width) {
}

void CLineSegment::Draw(ID2D1RenderTarget* pRT) const {
    ID2D1SolidColorBrush* pBrush = nullptr;
    pRT->CreateSolidColorBrush(m_color, &pBrush);

    pRT->DrawLine(m_start, m_end, pBrush, m_strokeWidth);

    if (pBrush) pBrush->Release();
}

std::shared_ptr<IDrawableObject> CLineSegment::Clone() const {
    return std::make_shared<CLineSegment>(m_start, m_end, m_color, m_strokeWidth);
}

// --- CEllipseSegment 実装 ---

CEllipseSegment::CEllipseSegment(D2D1_ELLIPSE ellipse, D2D1_COLOR_F color, float width)
    : m_ellipse(ellipse), m_color(color), m_strokeWidth(width) {
}

void CEllipseSegment::Draw(ID2D1RenderTarget* pRT) const {
    ID2D1SolidColorBrush* pBrush = nullptr;
    pRT->CreateSolidColorBrush(m_color, &pBrush);

    pRT->DrawEllipse(m_ellipse, pBrush, m_strokeWidth);

    if (pBrush) pBrush->Release();
}

std::shared_ptr<IDrawableObject> CEllipseSegment::Clone() const {
    return std::make_shared<CEllipseSegment>(m_ellipse, m_color, m_strokeWidth);
}


// --- CAddObjectCommand 実装 ---

CAddObjectCommand::CAddObjectCommand(CDocument* pDoc, std::shared_ptr<IDrawableObject> object)
    : m_pDoc(pDoc), m_object(object), m_index(0) {
}

void CAddObjectCommand::Execute() {
    m_index = m_pDoc->GetLastObjectIndex();
}

void CAddObjectCommand::Undo() {
    m_pDoc->RemoveObjectAt(m_index);
}

// --- CComplementCommand 実装 ---

CComplementCommand::CComplementCommand(CDocument* pDoc, size_t index, std::shared_ptr<IDrawableObject> original, std::shared_ptr<IDrawableObject> newItem)
    : m_pDoc(pDoc), m_index(index), m_originalObject(original), m_newObject(newItem) {
}

void CComplementCommand::Execute() {
    m_pDoc->ReplaceObject(m_index, m_newObject);
}

void CComplementCommand::Undo() {
    m_pDoc->ReplaceObject(m_index, m_originalObject);
}

// --- CDocument 実装 ---

void CDocument::AddObject(std::shared_ptr<IDrawableObject> object, bool recordCommand) {
    m_objects.push_back(object);
    if (recordCommand) {
        m_redoStack = {};
        RecordCommand(std::make_unique<CAddObjectCommand>(this, object));
    }
}

void CDocument::ReplaceObject(size_t index, std::shared_ptr<IDrawableObject> newObject) {
    if (index < m_objects.size()) {
        m_objects[index] = newObject;
    }
}

void CDocument::RemoveObjectAt(size_t index) {
    if (index < m_objects.size()) {
        m_objects.erase(m_objects.begin() + index);
    }
}

void CDocument::DrawAll(ID2D1RenderTarget* pRT) const {
    for (const auto& obj : m_objects) {
        obj->Draw(pRT);
    }
}

std::shared_ptr<IDrawableObject> CDocument::GetLastObject() const {
    return m_objects.empty() ? nullptr : m_objects.back();
}

size_t CDocument::GetLastObjectIndex() const {
    return m_objects.empty() ? 0 : m_objects.size() - 1;
}

void CDocument::RecordCommand(std::unique_ptr<ICommand> command) {
    m_undoStack.push(std::move(command));
}

void CDocument::Undo() {
    if (!CanUndo()) return;

    std::unique_ptr<ICommand> command = std::move(m_undoStack.top());
    m_undoStack.pop();

    command->Undo();
    m_redoStack.push(std::move(command));
}

void CDocument::Redo() {
    if (!CanRedo()) return;

    std::unique_ptr<ICommand> command = std::move(m_redoStack.top());
    m_redoStack.pop();

    command->Execute();
    m_undoStack.push(std::move(command));
}