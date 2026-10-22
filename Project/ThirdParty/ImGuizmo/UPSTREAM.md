# ImGuizmo

Official source: https://github.com/CedricGuillemet/ImGuizmo

Pinned commit: `105e4fde45e4e74aae8973d98750820ef7aca7e2`

Imported files: `src/ImGuizmo.h`, `src/ImGuizmo.cpp`, `LICENSE` (MIT).
Compile with `ThirdParty/imgui` on the include path.

Local fixes in `ImGuizmo.cpp`:

- `GetScaleType`: test the projected axis segment against the current mouse position,
  avoiding an undefined ray/plane intersection in camera-aligned views.
- `HandleScale`: pick a camera-facing interaction plane containing the selected axis,
  matching translation's strategy. Fixed model-axis planes can pass through the camera
  and produce zero/unstable scale ratios in front/side views.

Covered by `OffsetMeshGizmoInteraction` in the editor tests.
