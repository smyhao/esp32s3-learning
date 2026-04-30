set(BSP_SRC_DIRS "LED")
set(BSP_INCLUDE_DIRS "LED")
set(BSP_REQUIRES driver)

idf_component_register(SRC_DIRS ${BSP_SRC_DIRS}
                    INCLUDE_DIRS ${BSP_INCLUDE_DIRS}
                    REQUIRES ${BSP_REQUIRES})

component_compile_options(
    -O3
    -Wno-format
)
