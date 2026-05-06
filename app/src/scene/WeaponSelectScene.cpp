#include "WeaponSelectScene.h"
#include "GameScene.h"
#include "Input.h"
#include "SceneManager.h"
#include <algorithm>
#include <memory>

#ifndef IMGUI_DISABLED
#include "imgui.h"
#endif // IMGUI_DISABLED

namespace {
constexpr int kWeaponCount = 3;

PlayerWeaponType ToWeaponType(int index) {
    switch (index) {
    case 1:
        return PlayerWeaponType::Dual;
    case 2:
        return PlayerWeaponType::GreatSword;
    case 0:
    default:
        return PlayerWeaponType::Standard;
    }
}
} // namespace

void WeaponSelectScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    selectedIndex_ = 0;
    startRequested_ = false;
    requestedWeaponType_ = PlayerWeaponType::Standard;
}

void WeaponSelectScene::Update() {
    Input *input = ctx_->input;

    if (input->IsKeyTrigger(DIK_LEFT) || input->IsKeyTrigger(DIK_A)) {
        selectedIndex_ = (selectedIndex_ + kWeaponCount - 1) % kWeaponCount;
    }
    if (input->IsKeyTrigger(DIK_RIGHT) || input->IsKeyTrigger(DIK_D)) {
        selectedIndex_ = (selectedIndex_ + 1) % kWeaponCount;
    }

    if (input->IsKeyTrigger(DIK_1)) {
        selectedIndex_ = 0;
        StartGame(ToWeaponType(selectedIndex_));
    } else if (input->IsKeyTrigger(DIK_2)) {
        selectedIndex_ = 1;
        StartGame(ToWeaponType(selectedIndex_));
    } else if (input->IsKeyTrigger(DIK_3)) {
        selectedIndex_ = 2;
        StartGame(ToWeaponType(selectedIndex_));
    } else if (input->IsKeyTrigger(DIK_RETURN) ||
               input->IsKeyTrigger(DIK_SPACE)) {
        StartGame(ToWeaponType(selectedIndex_));
    }

    if (input->IsGamepadConnected()) {
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT)) {
            selectedIndex_ =
                (selectedIndex_ + kWeaponCount - 1) % kWeaponCount;
        }
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT)) {
            selectedIndex_ = (selectedIndex_ + 1) % kWeaponCount;
        }
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)) {
            StartGame(ToWeaponType(selectedIndex_));
        }
    }

    if (startRequested_) {
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(requestedWeaponType_));
    }
}

void WeaponSelectScene::Draw() { DrawDebugUi(); }

void WeaponSelectScene::StartGame(PlayerWeaponType weaponType) {
    requestedWeaponType_ = weaponType;
    startRequested_ = true;
}

void WeaponSelectScene::DrawDebugUi() {
#ifndef IMGUI_DISABLED
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Weapon Select", nullptr, flags);

    ImGui::SetCursorPos({56.0f, 48.0f});
    ImGui::TextUnformatted("武器を選ぶ");
    ImGui::SetCursorPosX(56.0f);
    ImGui::TextUnformatted(
        "1 / 2 / 3、左右キー、Enterで決定。戦い方そのものが変わります。");

    const char *names[kWeaponCount] = {"通常剣", "双剣", "大剣"};
    const char *roles[kWeaponCount] = {
        "攻撃、ガード、カウンターがそろった扱いやすい一本。",
        "ガードを捨てて、左右の手数と交互カウンターで押す二本。",
        "ガードで溜め、溜めた一撃と満タンカウンターで大きな隙を作る一本。"};
    const char *tips[kWeaponCount] = {
        "RMB / LTでガード。ガード中に振るとカウンター。",
        "RMB / Bで左右交互カウンター。ガード軽減はなし。",
        "RMB / LTでチャージ。約3割以上溜めると振れます。"};

    const float startY = 150.0f;
    const float cardW = (viewport->WorkSize.x - 144.0f) / 3.0f;
    const float cardH = 310.0f;
    for (int i = 0; i < kWeaponCount; ++i) {
        const float x = 56.0f + i * (cardW + 16.0f);
        ImGui::SetCursorPos({x, startY});
        ImGui::PushID(i);
        const bool selected = selectedIndex_ == i;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.24f, 0.42f, 0.62f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  {0.30f, 0.50f, 0.72f, 1.0f});
        }

        if (ImGui::Button(names[i], {cardW, 48.0f})) {
            selectedIndex_ = i;
            StartGame(ToWeaponType(i));
        }

        if (selected) {
            ImGui::PopStyleColor(2);
        }

        ImGui::BeginChild("detail", {cardW, cardH - 54.0f}, true);
        ImGui::TextWrapped("%s", roles[i]);
        ImGui::Separator();
        ImGui::TextWrapped("%s", tips[i]);
        ImGui::EndChild();
        ImGui::PopID();
    }

    ImGui::SetCursorPos({56.0f, startY + cardH + 28.0f});
    ImGui::TextUnformatted(
        "剣を振る攻撃には短い隙が入り、武器ごとに隙・防御・カウンターの価値が変わります。");

    ImGui::End();
    ImGui::PopStyleVar(2);
#endif // IMGUI_DISABLED
}
