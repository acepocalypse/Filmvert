# NegPy study for Filmvert

Studied on 2026-10-11: [marcinz606/NegPy](https://github.com/marcinz606/NegPy), version 0.64.0, commit `ab272a0a8cd8e9fd79bc8680587db8454813289e`.

Scope: source and documentation review of geometry, analysis regions, normalization, print curves, automatic controls, roll baselines, and preview/export consistency. NegPy was not installed or run; this is an implementation comparison, not a measured comparison of image quality. Filmvert's existing code was read for comparison.

## Geometry and analysis regions

NegPy resolves the crop selection before metering but postpones the output pixel crop until later in the pipeline. `resolve_analysis_region` selects one pixel region for the meters: a valid manual analysis rectangle takes precedence over the crop and disables the symmetric analysis inset. An absent or degenerate rectangle falls back to the crop plus inset. Its rectangle uses normalized coordinates in the transformed image.

The normalization processor uses that resolved region and includes the manual rectangle in its analysis-cache invalidation checks. Viewport/source conversions live in a shared coordinate-mapping service. There are tests proving that choosing different analysis regions changes measured bounds, and that the full-frame crop-editing preview retains the actual crop ROI on CPU and GPU.

Implication for Filmvert: define a single analysis-region contract used by the overlay, dragging, CPU metering, and persistence. Filmvert currently stores four source-space corners, rather than NegPy's transformed-image rectangle. Keep that distinction explicit; copying the rectangle convention alone would break saved regions. Test the measured pixels as well as the drawn handles after applying, adjusting, rotating, and undoing a crop.

Sources: [normalization.py](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/negpy/features/exposure/normalization.py), [coordinate_mapping.py](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/negpy/services/view/coordinate_mapping.py), [analysis-region tests](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/tests/test_analysis_region.py), [crop-preview parity tests](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/tests/test_gpu_crop_preview_full_parity.py).

## Conversion and metering

NegPy estimates bounds in logarithmic signal space. A block-median grid reduces small outliers; percentile measurements establish bounds. Tonal-range sampling and per-channel color deviations are separate. At the dense end, a shared set of low-chroma pixels is preferred when trustworthy neutrals exist; otherwise it falls back to per-channel statistics. Normalized values can extend beyond the nominal range, leaving downstream tone shaping to roll them off.

Filmvert instead blurs a film-base reciprocal inversion, finds the minimum and maximum luminance pixels inside its quadrilateral, then takes each selected pixel's three channels as the corresponding endpoint. That couples endpoint color to the scene colors at two locations. A colored highlight or shadow can therefore affect the conversion's channel balance.

First experiment: add an optional robust analysis mode that reports percentile bounds and samples neutral candidates separately from tonal extremes. Compare real scans containing saturated highlights, colored shadows, dust, flat scenes, and small analysis regions. Retain the existing mode for comparison and saved-edit compatibility.

Sources: NegPy `analyze_log_exposure_bounds_from_log` and `_same_pixel_color_floor_refs` in normalization.py; Filmvert `src/image/imageProcessing.cpp` (`blurImage`, `processMinMax`) and `src/gpu/glslKernels.h` (`imgProcess`).

## Print shaping and controls

NegPy shapes print density through a toe, a central slope, and a shoulder. Numerically stable softplus functions soften the ends; density becomes linear reflectance through `10 ** (-D)`, followed by output encoding. Automatic density and grade derive from metered scene information. The slider adapter exposes manual-equivalent values while storing offsets against those meters.

Filmvert's everyday tone controls are currently Lift, Gamma, Gain, black/white points, Offset, Multiply, and curves. A print-style transfer curve is an engine feature, not a label change. It needs an explicit mode, serialization, rendering integration, and reference-image comparisons before it can safely replace an existing rendering path.

Keep Filmvert's single controls panel. The small workflow improvement worth considering first is an automatic starting point with familiar brightness/contrast adjustments; postpone paper profiles and specialized darkroom tools.

Sources: [exposure/logic.py](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/negpy/features/exposure/logic.py), [auto_sliders.py](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/negpy/features/exposure/auto_sliders.py).

## Rolls and rendering consistency

NegPy's roll-bound pooling treats luminance and color independently and rejects frames with atypical color offsets when enough frames are available. The user can apply the pooled tonal range and color balance separately, preserve a frame's own result, or pool a scene independently. This differs from merely copying one frame's endpoints to every other frame.

Its analysis grid is built from the pre-geometry source and replays geometry at analysis scale, helping decouple the meters from preview resolution. CPU/GPU and crop-preview tests cover numerical and ROI consistency. These are useful test patterns for Filmvert's proxy/full-resolution paths.

Sources: `pool_frame_bounds`, `mix_luma_color_bounds`, and `analysis_grid` in normalization.py; [pipeline documentation](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/docs/PIPELINE.md).

## Color-space boundary

NegPy's native RAW path operates on linear sensor channels with unity white balance and fixed decode scaling. Filmvert requests camera white balance and ACES output from LibRaw, then converts toward its working pipeline. NegPy also documents an assumed output boundary profile for its native-channel pipeline. These are different contracts: do not transplant channel-specific matrices or density processing without specifying the data they expect.

Sources: [raw_demosaic.py](https://github.com/marcinz606/NegPy/blob/ab272a0a8cd8e9fd79bc8680587db8454813289e/negpy/infrastructure/capture/raw_demosaic.py), pipeline documentation, Filmvert `src/image/imageIO.cpp`.

## Suggested order

1. Finish crop/analysis correctness with tests connecting displayed handles to the pixels actually metered.
2. Prototype robust bounds and independent neutral sampling behind an optional analysis mode.
3. Add pooled roll/scene color balance while preserving per-frame tone where appropriate.
4. Evaluate automatic brightness/contrast and a print-style tone curve on representative scans.

NegPy declares GPL-3.0; Filmvert currently has an MIT license. This study records design concepts and source provenance, without copying NegPy implementation code into Filmvert.

## Filmvert prototype

Implemented an optional **Robust Analysis** checkbox in the existing panel. The implementation is independent and retains Filmvert's film-base reciprocal inversion and working color-space contract. It does not reproduce NegPy's log-density print engine.

The sampler uses at most 128×128 cells, nine source taps per cell, and per-channel medians. Both cell centers and taps must be inside the selected quadrilateral. Tone uses 0.5th/99.5th-percentile luminance endpoints. Color estimates prefer low-chroma samples in the lower/upper 20% luminance bands; if fewer than eight candidates exist, they fall back to per-channel percentile endpoints. Their color deviations are anchored to the separately metered luminance. Degenerate regions are rejected without modifying the conversion. These thresholds are heuristics and need comparisons on representative real scans.

Scratch storage stays below 1 MiB with the fixed sample cap. The mode adds no image-sized analysis buffers, dependencies, shader stages, or work during slider edits. Existing images retain their saved endpoint values; the selected analysis method is a global preference used only on the next explicit Analyze action.

Release-build benchmark on this machine, synthetic 3000×2000 grayscale ramp: new sampling/statistics **4.09 ms**, existing full-frame blur alone **527.04 ms**. Decode, display rendering, export, and the shared OCIO endpoint adjustment are excluded. This is a single-run timing, not a guarantee for every image or machine. Run `Build-release/bin/filmvert_tests.exe '[robust-benchmark]' --reporter compact` to reproduce.

Validation: the full Catch2 suite passed **187 cases / 2,641 assertions** when allowed to write Filmvert's normal log directory. Focused robust/viewer tests include metered crop boundaries, skewed regions, sparse dust, saturated highlights, film-base compensation, invalid/flat regions, persisted preferences, and sample-cap/resolution consistency.
