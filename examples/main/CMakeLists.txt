idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES ps_pwm driver) # ps_pwm 컴포넌트에 대한 의존성