/*
 * Projet Unix 2012 : Code de Vinci
 *
 * Partie en tête du serveur
 */
#ifndef SERVER_H
#define SERVER_H

#include "project.h"

#define SHM_KEY_1 0x231285	/* clé pour la mémoire partagée 1 */
#define SHM_KEY_2 0x851223	/* clé pour la mémoire partagée 2 */
#define SEM_KEY 0x011287	/* clé pour la sémaphore 1 */
#define SEM_MUTEX 0			/* numéro du sémaphore pour le contrôle de l'accès au nombre de lecteurs */
#define SEM_SHARED 1		/* numéro du sémaphore pour le contrôle de l'accès à la mémoire partagée */
#define ALRM_TIME 30		/* Temps de l'alarme avant la fin des inscriptions */

/* Signatures des méthodes */
void init(void);
void end(void);
void app(void);
void sigchld_handler(int);
void alarm_handler(int);
void update_shared_mem(void);
void termination_handler(int);
void start_game(void);
SOCKET init_connection(void);
void delete_player(int);
void do_turn(void);
void create_shared_mem(void);
void create_shared_readers(void);
void create_semaphore(void);
void down(int);
void up(int);
GameBoard get_shared_board(void);
void send_to_all(Message);
void end_game(void);

/* Structure pour garder les informations relatives à un joueur */
typedef struct
{
	char username[BUF_SIZE];
	int fd_w; // pipe d'écriture
	int fd_r; // pipe de lecture
	int score;
	int connected;
	int hidden_tokens; //nombre de pieces qu'il reste a decouvrire
	int playing;
} UserInfo;

/* Structure de la mémoire partagée */
typedef struct
{
	GameBoard board;
} SharedMem;

#endif	/* SERVER_H */
