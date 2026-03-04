#pragma once
#include <DirectXMath.h>

class Camera {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="aspect">アスペクト比</param>
    void Initialize(float aspect);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    // Setter
    void SetPosition(const DirectX::XMFLOAT3 &pos) { position_ = pos; }
    void SetRotation(const DirectX::XMFLOAT3 &rot) { rotation_ = rot; }

    // Getter
    const DirectX::XMMATRIX &GetView() const { return view_; }
    const DirectX::XMMATRIX &GetProj() const { return proj_; }
    const DirectX::XMFLOAT3 &GetPosition() const { return position_; }
    const DirectX::XMFLOAT3 &GetRotation() const { return rotation_; }

  private:
    DirectX::XMFLOAT3 position_{0.0f, 0.0f, -5.0f};
    DirectX::XMFLOAT3 rotation_{0.0f, 0.0f, 0.0f};

    float fovY_ = DirectX::XM_PIDIV4;
    float aspect_ = 1.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;

    DirectX::XMMATRIX view_{};
    DirectX::XMMATRIX proj_{};
};
