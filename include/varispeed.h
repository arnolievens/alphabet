/**
 * @author      : Arno Lievens (arnolievens@gmail.com)
 * @created     : 13/09/2021
 * @filename    : varispeed.h
 */

#ifndef VARISPEED_H
#define VARISPEED_H

#include "../include/player.h"

typedef struct {
    Player* player;
    GtkWidget* box;
    GtkWidget* spin;
} Varispeed;

extern Varispeed* varispeed_new(Player* player);

extern void varispeed_free(Varispeed* this);

#endif

// vim:ft=c

