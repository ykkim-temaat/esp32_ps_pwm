set(sources "ps_pwm.c")

idf_component_register(SRCS "${sources}"
                       INCLUDE_DIRS "include"
                       REQUIRES "driver"
)
