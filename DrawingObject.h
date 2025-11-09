#pragma once

#include <d2d1.h>
#include <vector>
#include <memory>
#include <stack>
#include <algorithm> // std::max, std::min を使用するために必要
#include <cmath>     // std::abs, std::sqrt を使用するために必要

// 前方宣言
class CDocument;

// --- 描画オブジェクトの抽象基底クラス ---
class IDrawableObject {
public:
    virtual ~IDrawableObject() = default;
    virtual void Draw(ID2D1RenderTarget* pRT) const = 0;
    virtual std::shared_ptr<IDrawableObject> Clone() const = 0;
    virtual void Complement() = 0; // AI補完ロジックを適用
    virtual bool IsComplementable() const = 0; // 補完可能か判定
};

// --- 直線セグメント（補完結果として使用） ---
class CLineSegment : public IDrawableObject {
private:
    D2D1_POINT_2F m_start;
    D2D1_POINT_2F m_end;
    D2D1_COLOR_F m_color;
    float m_strokeWidth;

public:
    CLineSegment(D2D1_POINT_2F start, D2D1_POINT_2F end, D2D1_COLOR_F color, float width);

    // IDrawableObjectをオーバーライド
    void Draw(ID2D1RenderTarget* pRT) const override;
    std::shared_ptr<IDrawableObject> Clone() const override;
    void Complement() override {}
    bool IsComplementable() const override { return false; }
};

// --- 楕円セグメント（補完結果として使用） ---
class CEllipseSegment : public IDrawableObject {
private:
    D2D1_ELLIPSE m_ellipse;
    D2D1_COLOR_F m_color;
    float m_strokeWidth;

public:
    CEllipseSegment(D2D1_ELLIPSE ellipse, D2D1_COLOR_F color, float width);

    // IDrawableObjectをオーバーライド
    void Draw(ID2D1RenderTarget* pRT) const override;
    std::shared_ptr<IDrawableObject> Clone() const override;
    void Complement() override {}
    bool IsComplementable() const override { return false; }
};


// --- フリーハンドストローク ---
class CFreehandStroke : public IDrawableObject {
public:
    enum class ShapeType { None, Line, Ellipse, Curve };
private:
    std::vector<D2D1_POINT_2F> m_points;
    D2D1_COLOR_F m_color;
    float m_strokeWidth;
    bool m_isComplemented;

public:
    ShapeType m_detectedShape = ShapeType::None;
    D2D1_ELLIPSE m_complementEllipse = { 0 }; // 検出された楕円データ

public:
    CFreehandStroke(D2D1_COLOR_F color, float width);
    void AddPoint(D2D1_POINT_2F p);

    // IDrawableObjectをオーバーライド
    void Draw(ID2D1RenderTarget* pRT) const override;
    std::shared_ptr<IDrawableObject> Clone() const override;
    void Complement() override;
    bool IsComplementable() const override;

    const std::vector<D2D1_POINT_2F>& GetPoints() const { return m_points; }
};


// --- コマンド抽象基底クラス（Undo/Redo用） ---
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0; // 実行
    virtual void Undo() = 0;    // 元に戻す
};

// --- オブジェクト追加コマンド ---
class CAddObjectCommand : public ICommand {
private:
    CDocument* m_pDoc;
    std::shared_ptr<IDrawableObject> m_object;
    size_t m_index; // 挿入位置 (Undo/Redo用)

public:
    CAddObjectCommand(CDocument* pDoc, std::shared_ptr<IDrawableObject> object);

    // ICommandをオーバーライド
    void Execute() override;
    void Undo() override;
};

// --- AI補完コマンド (Undo/Redoを可能にする) ---
class CComplementCommand : public ICommand {
private:
    CDocument* m_pDoc;
    std::shared_ptr<IDrawableObject> m_originalObject; // 補完前のオブジェクト
    std::shared_ptr<IDrawableObject> m_newObject;      // 補完後のオブジェクト
    size_t m_index; // オブジェクトがリストにあった位置

public:
    CComplementCommand(CDocument* pDoc, size_t index, std::shared_ptr<IDrawableObject> original, std::shared_ptr<IDrawableObject> newItem);

    // ICommandをオーバーライド
    void Execute() override;
    void Undo() override;
};


// --- ドキュメント管理クラス ---
class CDocument {
private:
    std::vector<std::shared_ptr<IDrawableObject>> m_objects;
    std::stack<std::unique_ptr<ICommand>> m_undoStack;
    std::stack<std::unique_ptr<ICommand>> m_redoStack;

public:
    // ドキュメント操作
    void AddObject(std::shared_ptr<IDrawableObject> object, bool recordCommand = true);
    void ReplaceObject(size_t index, std::shared_ptr<IDrawableObject> newObject);
    void RemoveObjectAt(size_t index);

    // 描画
    void DrawAll(ID2D1RenderTarget* pRT) const;

    // Undo/Redo
    void Undo();
    void Redo();
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    // 現在のオブジェクトアクセス（補完用）
    std::shared_ptr<IDrawableObject> GetLastObject() const;
    size_t GetLastObjectIndex() const;

    // コマンド記録
    void RecordCommand(std::unique_ptr<ICommand> command);
};