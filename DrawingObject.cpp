#include "DrawingObject.h"

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
    return clone;
}

bool CFreehandStroke::IsComplementable() const {
    // 補完済みでない、かつ点数が十分にあることを確認
    return !m_isComplemented && m_points.size() >= 2;
}

// 簡易AI補完ロジック: ほぼ直線なら直線に補完可能とフラグを立てる
void CFreehandStroke::Complement() {
    if (m_points.size() < 2) return;

    D2D1_POINT_2F start = m_points.front();
    D2D1_POINT_2F end = m_points.back();

    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float L_sq = dx * dx + dy * dy;
    float L = std::sqrt(L_sq);

    // 許容誤差 (ストローク幅の2倍)
    const float MAX_DEVIATION = m_strokeWidth * 2.0f;

    float maxDeviation = 0.0f;

    // 各点と始点・終点を結ぶ直線との距離を計算
    for (const auto& p : m_points) {
        // A*x + B*y + C = 0 の距離計算
        float A = end.y - start.y;
        float B = start.x - end.x;
        float C = end.x * start.y - end.y * start.x;

        float distance = std::abs(A * p.x + B * p.y + C) / (L > 0.0f ? L : 1.0f);
        maxDeviation = max(maxDeviation, distance);
    }

    // 最大偏差が許容範囲内であれば、補完可能とマーク
    if (maxDeviation < MAX_DEVIATION) {
        m_isComplemented = true;
    }
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

// --- CAddObjectCommand 実装 ---

CAddObjectCommand::CAddObjectCommand(CDocument* pDoc, std::shared_ptr<IDrawableObject> object)
    : m_pDoc(pDoc), m_object(object), m_index(0) {
}

void CAddObjectCommand::Execute() {
    // 最後に挿入されたインデックスを保存
    m_index = m_pDoc->GetLastObjectIndex();
}

void CAddObjectCommand::Undo() {
    // 保存したインデックスのオブジェクトを削除
    m_pDoc->RemoveObjectAt(m_index);
}

// --- CComplementCommand 実装 ---

CComplementCommand::CComplementCommand(CDocument* pDoc, size_t index, std::shared_ptr<IDrawableObject> original, std::shared_ptr<IDrawableObject> newItem)
    : m_pDoc(pDoc), m_index(index), m_originalObject(original), m_newObject(newItem) {
}

void CComplementCommand::Execute() {
    // 補完後のオブジェクトに置き換える
    m_pDoc->ReplaceObject(m_index, m_newObject);
}

void CComplementCommand::Undo() {
    // 元のオブジェクトに戻す
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
    // インデックス位置の要素を削除
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
    // リストが空でなければ、最後の要素のインデックスを返します。
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