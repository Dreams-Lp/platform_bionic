ifneq (generic, $(TARGET_CPU_VARIANT))
libm_arch_src_files_arm64 := \
    arm64/s_floor.S \
    arm64/s_floorf.S \
    arm64/s_ceil.S \
    arm64/s_ceilf.S \
    arm64/e_sqrt.S \
    arm64/e_sqrtf.S
else
libm_arch_src_files_arm64 := $(libm_arch_src_files_default)
endif
