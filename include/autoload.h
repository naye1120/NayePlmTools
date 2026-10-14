#pragma once
#include <stddef.h>

typedef enum {
    AUTOLOAD_DISABLED, AUTOLOAD_WAITING, AUTOLOAD_COUNTDOWN, AUTOLOAD_EXECUTING,
    AUTOLOAD_DONE, AUTOLOAD_ABORTED, AUTOLOAD_FAILED
} AutoloadState;

typedef struct {
    AutoloadState state;
    int remaining, total, done, successful, failed, skipped;
    int delay, scan_usb_payloads;
    long long remaining_ms;
    int entry_index, entry_duration_ms;
    long long entry_remaining_ms;
    char current[128];
    char list[4096];
    char outcomes[4096]; /* Per payload: p=pending, s=success, f=failed, k=skipped. */
} AutoloadSnapshot;

/* One run per start/stop lifecycle. Repeated start is idempotent, even after
 * completion/reset. Stop cooperatively cancels and joins the owned worker.
 * Never call stop from the worker or a callback executing inside it. */
int pldmgr_autoload_start(void);
void pldmgr_autoload_stop(void);
void pldmgr_autoload_abort(void);
/* Ignore reset while waiting/counting/executing; clear terminal display only. */
void pldmgr_autoload_reset(void);
/* Pure readers. Signal readiness separately after authorizing frontend access. */
void pldmgr_autoload_frontend_ready(void);
void pldmgr_autoload_get_snapshot(AutoloadSnapshot *out);
const char *pldmgr_autoload_state_name(AutoloadState state);
int pldmgr_autoload_get_remaining_seconds(void);
long long pldmgr_autoload_get_remaining_ms(void);
void pldmgr_autoload_get_status(int *total, int *done, char *current);
int pldmgr_autoload_update_config_entry(const char *old_filename, const char *new_filename);
