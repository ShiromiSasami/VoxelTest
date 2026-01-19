# VoxelTest

Unreal Engine 5 向けのボクセル描画サンドボックス。GPU 上で SDF を構築し、フルスクリーンのレイマーチで描画します。

## 概要
- インスタンスの中心/スケールからボクセル密度ボリュームを GPU で生成。
- シード生成 + Jump Flooding Algorithm (JFA) で SDF を構築。
- フルスクリーン三角形のレイマーチで SceneColor/SceneDepth に書き込み。
- 任意でボクセルのデバッグメッシュ表示が可能。

## 主要システム
- **ボリュームアセット**: `UVoxelVolume` がボクセル格子を構築し、レンダリング用リソース（中心/スケール）を保持。
- **レンダーコンポーネント**: `UVoxelRenderComponent` がボリューム参照を持ち、再構築やアニメ更新を行う。
- **アニメータコンポーネント**: `UVoxelVolumeAnimatorComponent` が中心/スケールのランタイムアニメを駆動。
- **レンダーパス**: `AddVoxelRaymarchPass` が密度生成、シード生成、JFA、SDF 変換、描画パスを構築。

## レンダリングパイプライン（概要）
1. インスタンス中心を `DensityTex` にスプラット (`VoxelDensity.usf`)。
2. 表面シード抽出 (`VoxelDistanceField.usf`)。
3. JFA で最近傍シードを伝播。
4. シード距離から `SDFTex` を生成。
5. `VoxelRaymarch.usf` でレイマーチして色/深度を出力。

## プロジェクト構成
- `Source/VoxelTest/` C++ モジュール
  - `Rendering/Voxel/` レンダリング + ボクセルボリューム/ランタイムアニメ
  - `Variant_Combat/`, `Variant_Platforming/`, `Variant_SideScrolling/` ゲームプレイのバリエーション
- `Shaders/Voxel/` シェーダ実装
- `Content/` アセット（Blueprint、マテリアル、メッシュ）
- `Config/` デフォルト設定
- エントリポイント: `VoxelTest.uproject`, `VoxelTest.sln`

## コンソール変数
- `r.Voxel.Raymarch` (0/1): レイマーチ描画パスの有効/無効。
- `r.Voxel.Debug` (0/1): ボクセルデバッグメッシュの有効/無効。

## ビルドと実行
- エディタ起動: `UnrealEditor VoxelTest.uproject`
- ビルド（IDE）: `VoxelTest.sln` を開き、`VoxelTestEditor`（Development）でビルド
- ビルド（Windows CLI）:
  ```
  <UE>/Engine/Binaries/DotNET/UnrealBuildTool.exe VoxelTestEditor Win64 Development -Project="<repo>/VoxelTest.uproject"
  ```
- ビルド（Linux/macOS）:
  ```
  <UE>/Engine/Build/BatchFiles/Build.sh VoxelTestEditor Linux Development -Project="<repo>/VoxelTest.uproject"
  ```
- 実行: `UnrealEditor VoxelTest.uproject -game -log`

## テスト
- ヘッドレス実行:
  ```
  UnrealEditor-Cmd VoxelTest.uproject -ExecCmds="Automation RunTests VoxelTest; Quit" -unattended -nop4 -nullrhi
  ```

## 注意
- 生成物フォルダはソース管理対象外: `Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/`.
