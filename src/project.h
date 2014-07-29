/*
 * Projet Unix 2012 : Code de Vinci
 *
 * Partie en tête partagée entre client et serveur
 */
#ifndef PROJET_H
#define PROJET_H

#include <sys/types.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <semaphore.h>
#include <time.h>
#include <arpa/inet.h>

#define SYS(call) ((call)==-1?perror(#call),exit(1):0)

#define BUF_SIZE 1024
#define MAX_PLAYERS 4
#define TRUE 1
#define FALSE 0
#define BLACK 'N'
#define WHITE 'B'
#define HIDDEN -1
#define NUM_TOKENS 24
#define CONFIRM_OK "OK"
#define CONFIRM_KO "KO"

#define MSG_STARTED 0
#define MSG_STRING 1
#define MSG_LOGIN 2
#define MSG_INIT_DECK 3
#define MSG_DECK 4
#define MSG_BOARD 5
#define MSG_TOKEN 6
#define MSG_ATTACK 7
#define MSG_OKKO 8
#define MSG_POS 9
#define MSG_LOGOUT 10

typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;

typedef struct
{
	char color; /* BLACK or WHITE */
	int num;
	char hidden; /* TRUE or FALSE */
} Token;

typedef struct
{
	int length;
	Token tokens[NUM_TOKENS];
} Deck;

typedef struct
{
	char id[MAX_PLAYERS][BUF_SIZE]; /* identifiants des joueurs */
	int length; /* nombre de joueurs */
	Deck decks[MAX_PLAYERS]; /* pièces des joueurs */
} GameBoard;

/* Structure d'un message échangé entre le client et le serveur */
typedef union
{
	int type; /* Type du message */

	struct
	{
		int type; /* MSG_STRING | MSG_LOGIN | MSG_SCORE */
		char msg[BUF_SIZE]; /* message à afficher */
	} msgString;

	struct
	{
		int type; /* MSG_TOKEN */
		Token t;
	} msgOneToken;

	struct
	{
		int type; /* MSG_INIT_DECK | MSG_DECK */
		Deck deck;
	} msgDeck;

	struct
	{
		int type; /* MSG_BOARD */
		GameBoard board;
	} msgBoard;

	struct
	{
		int type; /* MSG_ATTACK */
		int source;
		int target;
		int pos;
		int num_tok;
	} msgAttack;

	struct
	{
		int type; /* MSG_POS */
		Token t;
		int pos;
	} msgPos;

	struct
	{
		int type; /* MSG_LOGOUT */
		int player_id;
	} msgLogout;
} Message;

#endif	/* PROJET_H */
