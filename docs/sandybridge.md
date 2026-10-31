# Sandy Bridge local build

This experimental build targets the Intel Xeon E5-2689 (Sandy Bridge): AVX and SSE4.2, without AVX2, BMI2 or FMA3.
The upstream installer still requires x86-64-v3; build this version from source with the command below.

## Changes

- `tools/build_game.py --cpu sandybridge` targets both generated code and the runtime consistently.
- `runtime/soft_fma.h` implements fused multiply-add with integer arithmetic when `__FMA__` is absent. It keeps the
  product, sticky information, rounding modes, signed zero, overflow and subnormal handling. Multiplication followed
  by addition is not a substitute: it introduces double rounding. Builds with hardware FMA retain the upstream path.
- `runtime/main.c` checks the CPU requirements of the selected build in its self-test.
- Two upstream AMD fixes were backported: oversized depth/stencil images use separate storage
  (`vk_texture.c`, `bind_pool_memory`), and AMD's RDNA2-and-older driver uses pipeline-static alpha to coverage
  (`vk_pipeline.c`, `ensure_init`). The optional `vk_dyn_a2c` key is recognised by the configuration reader.

## Rebuild

From the repository root, after extraction and generation as described in [building.md](building.md):

```powershell
.venv\Scripts\python.exe tools\build_game.py --version v121 --cpu sandybridge --jobs 8
build\v121\totk.exe --selftest
```

The resulting executable is `build\v121\totk.exe`. The source NSPs and key values are never compiled into source files
tracked by git. Extraction and generated game code stay in ignored build directories.

The local shader translator was obtained from the official v0.1.9 release, and its supplied identity file was retained.
It passed `tools/xlate_smoke.py` on the Xeon. Shader preparation completed with zero translation failures; the cache
was packed with `tools/pack_shader_cache.py`.

## Validation on the target machine

- CPU: Intel Xeon E5-2689, 8 cores / 16 threads; approximately 20 GiB RAM.
- GPU: Radeon RX 6500 XT, 4 GiB; Windows driver reported as 26.6.4 by Vulkan.
- Game: 1.2.1. Base and update code and RomFS hashes match the project's expected hashes.
- `tests/diff/simd.py --encodings 20000 --cases 8 --cflags=-march=sandybridge --jobs 6`:
  1,048,640 encodings checked for validity, 159,976 execution cases, zero differences against Unicorn.
- `tests/diff/atomic.py --cflags=-march=sandybridge --jobs 2`: 198 tests, 12,672 cases, zero failures.
- `--selftest`, `--probe-loader`, `--probe-fs` and `--probe-vkmem` passed. The graphics memory probe reported zero
  validation errors.
- A bounded gameplay run completed 120 seconds of world time and exited normally. The owner also tested using a
  gamepad. After the AMD backports, a native screenshot in the opening cave showed no top-edge colour corruption and
  rendered Zelda's torch.
- A 60 FPS-limit experiment at the owner's 1600x900 game resolution measured 46.6 FPS mean and 42.6 FPS 1% low over
  the final 55.8-second reporting window. This is a scene-specific observation, not an open-world performance claim.
- A further run lasted 390.7 seconds of wall time, reached approximately 290 seconds of world time and closed normally
  through the game window. Many steady sections held 30 FPS. There were also stalls, including multi-second stalls near
  exit, so this run is not a claim of uninterrupted 30 FPS. The owner was changing live settings during local testing.

## Settings, saves and diagnostics

`build\v121\totk.ini` contains the owner's live settings. F1 opens the settings menu, F10 changes the performance
overlay, and F11 switches fullscreen. Set `fps_limit` in `[frame rate]` to 30, 60 or `unlimited`.
Unlimited removes the software pacer's wait and is also selectable in the F1 menu; the real-time step must be enabled.
`vk_present = immediate` avoids display-refresh pacing. The overlay labels an unlimited cap explicitly.
`totk.exe --probe-frame-rate` checks live transitions, invalid values, zero pacing for unlimited, and the fixed-step fallback.
`astc_bc = 1` reduces texture memory use for this 4 GiB GPU.

The executable's default profile is `build\profile`. Automated gameplay tests explicitly use `build\smoke-profile`,
or a separate copy of the imported profile. A Ryujinx save import was verified by file hashes and loaded successfully
in a copied test profile. Preserve a copy of an exported save before importing it into a profile.

### First-use stutter and persistent caches

Pretranslated shaders and complete graphics pipelines are different caches. Even when every shader is cached, a new
combination of shaders, render-target formats and render state may need a driver pipeline build. Texture uploads and
loading can also stall a new action. AMD RDNA2 uses the upstream non-GPL path because of driver issues; enabling GPL
indiscriminately is not a verified fix.

Keep `pipe_prebuild = 1` and `lib_prebuild = 1`, and retain `pipeline-keys.bin`, `pipeline-cache.bin`, `shader-answers.bin`
and `shader-cache/` next to the executable. Known pipelines are prepared before the guest starts on subsequent boots.
New combinations may still stutter the first time; prebuilding cannot predict every unseen combination.

The imported-save test first prewarmed 256 recorded pipelines; the next run prewarmed 887, with zero failures and zero
dropped records. The second run reused 1,163 shaders named by those keys without translating any. This verifies that
the cache learns and persists, rather than proving that all first-use stutter is eliminated. Native `--shot` verification
runs intentionally disable asynchronous recording; use ordinary gameplay or `TOTK_SHOTS` for cache-training runs.

Normal game logs are in `build\v121\logs\totk.log`. Bounded test logs and native screenshots are in that same
directory with descriptive names, including `rdna2-fixes.log` and `rdna2-fixes-native.png`.

These checks establish that the build runs on this CPU and GPU. They do not establish compatibility with every area,
quest, graphics effect or save in the game.
