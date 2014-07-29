/*
 * Projet Unix 2012 : Code de Vinci
 *
 * Partie en tête du client
 */
#ifndef CLIENT_H
#define CLIENT_H

#include "project.h"

#define SEP ";"
#define ATT_FORMAT "(Joueur;Position;Pièce)"

void app(const char *, const char *, const int);
SOCKET init_connection(const char *, const int);
void print_deck(const Deck *);
void print_board(const GameBoard *);
int compare(Token *, Token *);
int get_num_hiddens(const Deck *);

#endif
