APP_OBJS := $(patsubst %.c,$(APP_OBJ_DIR)/%.o,$(APP_SRCS))
APP_OBJS_QUOTED := $(foreach obj,$(APP_OBJS),"$(obj)")
TIMER_HUD_OBJS := $(patsubst $(TIMER_HUD_DIR)/%.c,$(HOST_OBJ_DIR)/timer_hud/%.o,$(TIMER_HUD_SUPPORT_SRCS))
TIMER_HUD_OBJS_QUOTED := $(foreach obj,$(TIMER_HUD_OBJS),"$(obj)")
