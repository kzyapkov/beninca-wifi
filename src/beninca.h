#ifndef _BENINCA_H
#define _BENINCA_H

#include "mgos.h"

#define BENINCA_SCA_SAMPLE_T            4000    /* us */
#define BENINCA_PUSH_DURATION           250     /* ms */
#define BENINCA_BLINKING_TIMEOUT        1250    /* ms */
#define BENINCA_OPENING_HALF_T         1.0
#define BENINCA_CLOSING_HALF_T         0.5

enum beninca_direction {
    dir_unknown,
    dir_open,
    dir_close,
};


#define BENINCA_CTL_STOP_HOLD       (1<<0)
#define BENINCA_CTL_STOP_STROBE     (1<<1)
#define BENINCA_CTL_PP_STROBE       (1<<2)


struct beninca_status {
    struct {
    bool was;
    double lasted;

    bool is;
    double since;
    } sca;

    double time;
    bool moving;
    double since;
    enum beninca_direction dir;
    struct {
        unsigned stop_hold:1;
        unsigned stop_strobe:1;
        unsigned pp_strobe:1;
    } control;
};

typedef void (*beninca_status_cb_t)(const struct beninca_status *);
void beninca_init(beninca_status_cb_t cb);
void beninca_deinit();

// one time push of the STOP control
void beninca_stop();

// toggle the STOP control to prevent movement
void beninca_stop_hold();
// release it
void beninca_stop_release();

// one time push of the P.P. control
void beninca_pp();

const char *beninca_sca_state_str(bool s);
const char *beninca_dir_str(enum beninca_direction d);
#endif // _BENINCA_H
