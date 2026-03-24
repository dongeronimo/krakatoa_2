# chisel_bridge — OpenChisel integration wrapper, compiled with aggressive
# optimizations regardless of the top-level build type.
#
# OpenChisel's ProjectionIntegrator::Integrate is a header template whose
# inner loop does per-voxel Eigen matrix math.  At -O0 this is 50-100×
# slower because nothing gets inlined and NEON/SIMD is disabled.
# Isolating the wrapper here keeps the rest of the project fully debuggable.

add_library(chisel_bridge STATIC
        chisel_manager.cpp
        chunk_serializer.cpp
)

# Own headers (chisel_manager.h, thread_event.h)
target_include_directories(chisel_bridge PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Parent directory for android_log.h and other shared headers
target_include_directories(chisel_bridge PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# OpenChisel + Eigen (header-only, inherited from open_chisel's PUBLIC dirs)
target_link_libraries(chisel_bridge PUBLIC
        open_chisel
)
target_link_libraries(chisel_bridge PRIVATE
        ${log-lib}
)

# Aggressive optimizations — squeeze every cycle out of the TSDF inner loop.
target_compile_options(chisel_bridge PRIVATE
        -O3                     # full optimization (inlining, vectorization, loop unroll)
        -g                      # keep debug symbols for crash stacks
        -DNDEBUG                # disable asserts
        -ffast-math             # allow FP reordering (safe for TSDF — no NaN/inf edge cases)
        -ffp-contract=fast      # fused multiply-add (NEON VFMA)
        -funroll-loops          # unroll tight voxel iteration loops
        -ftree-vectorize        # auto-vectorize with NEON
        -fomit-frame-pointer    # free up a register on ARM
        -Wno-error              # don't fail on warnings from Eigen/OpenChisel headers
)
