set(BSP_SRC_DIRS "LED")
set(BSP_INCLUDE_DIRS "LED")
set(BSP_REQUIRES driver)

idf_component_register(SRC_DIRS ${BSP_SRC_DIRS}
                    INCLUDE_DIRS ${BSP_INCLUDE_DIRS}
                    REQUIRES ${BSP_REQUIRES})

# -ffast-math: 允许激进的浮点优化，牺牲IEEE严格合规换取速度
# -O3: 最高级别优化，运行更快但编译更慢
# -Wno-format: 关闭format类警告(如printf格式串与参数不匹配)
component_compile_options(
    -ffast-math
    -O3
    -Wno-format
)
