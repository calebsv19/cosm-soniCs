#ifndef DAW_DAW_APP_MAIN_H
#define DAW_DAW_APP_MAIN_H

#include <stdbool.h>

bool daw_app_bootstrap(void);
bool daw_app_config_load(void);
bool daw_app_state_seed(void);
bool daw_app_subsystems_init(void);
bool daw_runtime_start(void);
void daw_app_set_legacy_entry(int (*legacy_entry)(void));
int daw_app_run_loop(void);
void daw_app_shutdown(void);

int daw_app_main_run(void);

#endif /* DAW_DAW_APP_MAIN_H */
