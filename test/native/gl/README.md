# PartsEngine2 GLES integration regression

This is a **synthetic renderer test, not a Dohna Dohna game-scene test**.
No game assets, game scripts or saved games are used.

The test compiles the production translation units (`video.c`, `draw.c`,
`parts.c`, `render.c`, `flat.c`, `construction.c`, `PartsEngine.c`, `activity.c`, EX lookup) and loads the real
GLES shaders. Test-only wrappers expose file-local entry points; production
rendering functions are not reimplemented or extracted as source text.
Unused VM/audio/game entry points are discarded by the linker.

Required on Linux: C compiler, SDL2 runtime/headers, cglm headers, GLES3 headers,
zlib headers, Mesa EGL/GLES with an offscreen software context. For example:

```sh
SDL2_INCLUDE=/usr/include/SDL2 CGLM_INCLUDE=/usr/include \
  ./xsystem4/test/native/gl/run.sh /tmp/parts-gles-test
```

The harness checks production shader links and actual GPU readback against
independently computed pixels:

* Production 3D shader loader: Reign/Tapir/Seal × regular/shadow/outline
  (9 pairs), including the 308-bone RGBA32F bone texture (no uniform block, so it
  links on drivers that count block members as vertex uniforms, e.g. SwiftShader). This catches
  capability/link failures that the 2D-only fixtures cannot detect. A successful
  host run is not proof that every emulator GPU backend accepts the shader.
* FLAT particle position modes 0/1/2, direct and nested timelines, two
  component placements with nonzero origin offsets, three animation frames,
  legacy and v2 transforms. Checks complete GPU readbacks rather than just
  matrix arithmetic; no game-specific offsets or assets are included.
* Construction H/V blur, full-size/subrect, pinned/unpinned draw target,
  RGB results, alpha preservation, two-pass blur.
* Drawing a solid fill with no active UV attribute, then copying it with a
  textured shader: verify pixels, no GL errors, and no enabled job attributes
  left on the shared VAO.
* Normal/LR/TB/combined sprite rendering through the real HLL setters,
  cached world matrix and CG renderer, including exact output position.
* Activity EX flag/list loading and CG output in **legacy and v2 modes**, all
  four LR/TB states crossed with all four signed magnification combinations.
  Exact placement/orientation checks ensure reversal is applied once, not twice.
* 32 actual SDL/GLES window/drawable replacements **with the GL context retained**.
  Verify the new surface starts black, then the production restore path presents
  the retained composited frame before any new script rendering. This does not
  simulate GL context loss or prove the reported Android game lifecycle race.
* Every actual `glDrawElements` is audited: texture unit 0 must not contain
  the texture attached to the draw framebuffer. The old blur implementation
  fails this invariant, regardless of whether a driver happens to look right.

Test-path substitutions are component lookup (returns the fixture component
instead of running the VM), UTF-8 key encoding identity for the in-memory EX
fixture (no SJIS/GBK game file), and guards for alpha-clipper construction and
widget sizing (abort if invoked; these fixtures only use unclipped CG parts).
A post-present modifier gameplay hook is a no-op in this renderer-only harness.
The SDL swap wrapper checks the rendered frame immediately before real swap.
Actual component dirtying, transforms, shader compilation, framebuffer changes,
texture copies, blend state and GL draws run unchanged production code.

Generated `*-gles-synthetic.ppm` files are raw GL readbacks of synthetic art.
They are useful diagnostic images, **not proof of the supplied game scenes**.
The plain C fixture/source-contract suite in the parent directory remains
complementary coverage for rotation and save-version handling.

On Android, the GLES vendor, renderer, and version are logged at startup.
The opt-in driver logging switches for Android and Linux GLES are defined in
[`src/video.c`](../../../src/video.c). Enable the appropriate switch before
launching the game.
Reproduce the issue while capturing `adb logcat -s libsys4:I` for errors and
undefined-behavior messages where the driver supports `GL_KHR_debug`.
Opting in requests a GLES debug context; if the driver rejects it, startup
logs the failure and retries with a normal context, which may emit fewer messages.
Disable the switch and relaunch when done.

A replacement-context negative control verifies that a new unshared context
starts with none of the previous context's textures. Two replacement-context
recovery tests then verify that `gfx_restore_window()` recovers shaders, FBOs,
the composited `main_surface`, and tracked textures (when backed up) or clears
stale handles and rebuilds rendering cleanly (when unbacked) on an unshared
replacement `EGLContext`.
