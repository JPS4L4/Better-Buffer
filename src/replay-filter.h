#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <obs.h>

const char *replay_filter_get_name(void);
const char *replay_filter_get_id(void);

void *replay_filter_create(obs_data_t *settings, obs_source_t *source);
void replay_filter_destroy(void *data);
void replay_filter_update(void *data, obs_data_t *settings);
obs_properties_t *replay_filter_properties(void *data);
void replay_filter_defaults(obs_data_t *settings);

#ifdef __cplusplus
}
#endif
