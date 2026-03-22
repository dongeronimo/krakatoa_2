# Truncated Signed Distance Field (TSDF)

## What is a Signed Distance Field?

A Signed Distance Field (SDF) is a 3D scalar field where every point in space stores the **distance to the nearest surface**:

- **Positive values** = the point is in front of the surface (free space, between camera and object)
- **Negative values** = the point is behind the surface (inside the object)
- **Zero** = the surface itself

```
Camera                                          Object interior
  |                                                  |
  |    [+1.0] [+0.8] [+0.5] [+0.2] [0.0] [-0.3] [-0.7] [-1.0]
  |    <------ positive ------>  surface  <---- negative ---->
```

The zero-crossing of this field defines the surface. Mesh extraction algorithms like **Marching Cubes** walk through the volume and generate triangles wherever the field crosses zero.

## Why "Truncated"?

Without truncation, you'd need to update **every voxel** along the entire depth ray for every pixel in every frame — extremely expensive and mostly useless.

The **truncation distance** defines a narrow band around the surface where values are actually stored and updated. Voxels outside this band are ignored:

```
  |    [clamp] [clamp] [+0.5] [+0.2] [0.0] [-0.3] [clamp] [clamp]
  |                    |<--- truncation band --->|
  |                      (e.g. 4cm each side)
```

Benefits of truncation:
- **Performance**: Only a thin shell of voxels is updated per frame instead of the full ray
- **Noise rejection**: Readings far from the estimated surface are discarded
- **Memory**: Sparse representations (like chunked volumes) only allocate near surfaces

## Truncation Distance vs Voxel Size

The ratio of truncation distance to voxel size matters:

| Ratio | Effect |
|-------|--------|
| Too small (1x) | Band is so narrow that noisy depth readings miss it, producing holes |
| Too large (20x) | Distance field is smeared, losing sharp detail |
| Sweet spot (3-5x) | Absorbs sensor noise while preserving surface detail |

In Krakatoa, with **1cm voxels** and **4cm truncation**, we update voxels within +/-4cm of each depth reading.

## Volumetric Fusion

When multiple depth frames observe the same surface, TSDF values are **averaged** using weighted running means:

```
TSDF_new = (TSDF_old * W_old + tsdf_measured * w_frame) / (W_old + w_frame)
W_new    = min(W_old + w_frame, W_max)
```

This weighted averaging is what makes TSDF reconstruction robust:
- Noisy individual readings cancel out over time
- The surface converges to a smooth, accurate estimate
- Weight capping (`W_max`) prevents early observations from dominating forever, allowing the field to adapt to moving objects

## Voxel Storage

Each voxel stores two values:
- **Distance** (float or fixed-point): the signed distance to the nearest surface, clamped to `[-truncationDist, +truncationDist]`
- **Weight** (integer or float): how many observations contributed to this voxel's estimate

## Mesh Extraction

The **Marching Cubes** algorithm extracts a triangle mesh from the TSDF volume:

1. For each 2x2x2 cell of voxels, sample the 8 corner TSDF values
2. Determine which corners are positive (outside) and which are negative (inside)
3. Use a lookup table to determine the triangle configuration (256 possible cases)
4. Interpolate vertex positions along edges where the sign changes (zero-crossing)
5. Compute normals from the TSDF gradient

## Coordinate Systems in Krakatoa

The TSDF integration must reconcile three coordinate systems:

| System | Convention | Forward | Up |
|--------|-----------|---------|-----|
| ARCore (OpenGL) | Right-handed | -Z | +Y |
| OpenChisel (CV) | Right-handed | +Z | -Y |
| Depth image | Row-major | into scene | -row |

The `ChiselManager` handles these conversions:
- The sensor-oriented view matrix from `ArCamera_getPose` is inverted to get camera-to-world
- A Y/Z flip matrix converts from OpenGL convention to OpenCV convention
- The resulting `cameraPose` transform maps OpenChisel's camera space to world space

## References

- Curless & Levoy, "A Volumetric Method for Building Complex Models from Range Images" (1996) — the original TSDF paper
- Newcombe et al., "KinectFusion: Real-Time Dense Surface Mapping and Tracking" (2011) — real-time TSDF on GPU
- Lorensen & Cline, "Marching Cubes: A High Resolution 3D Surface Construction Algorithm" (1987)
