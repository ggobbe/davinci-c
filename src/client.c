/*
 * Projet Unix 2012 : Code de Vinci
 *
 * Partie client
 */
#include "client.h"

typedef int (*cmpfct)(const void*, const void*);

char my_login[BUF_SIZE]; /* Pour conserver son login */
int my_id; /* Pour conserver son identifiant */
int my_turn_attack = FALSE; /* Pour savoir si c'est à son tour d'attaquer */
int my_turn_confirm = FALSE; /* Pour savoir si c'est à son tour de confirmer une attaque */
int my_turn_pos = FALSE; /* Pour savoir si c'est à son tour d'envoyer la position de la pièce courante */
GameBoard board; /* Plateau de jeu (ce que l'on en connait avec les pièces encore cachées */
Token current; /* Pièce courante reçue du serveur */

void app(const char* address, const char* login, const int port)
{
	Message msg_login;
	char buffer[BUF_SIZE];
	int n;
	fd_set fds;
	SOCKET sock;
	int connected[MAX_PLAYERS]; /* Pour empêcher d'attaquer quelqu'un qui s'est déconnecté à ce tour */
	int d;

	for (d = 0; d < MAX_PLAYERS; d++)
	{
		connected[d] = TRUE;
	}

	/* Socket vers le serveur */
	sock = init_connection(address, port);

	/* On garde le pseudo et on l'envoie au serveur */
	strcpy(my_login, login);
	msg_login.msgString.type = MSG_LOGIN;
	sprintf(msg_login.msgString.msg, "%s", login);
	SYS(write(sock, &msg_login, sizeof(msg_login)));

	/* Boucle principale du client */
	while (TRUE)
	{
		/* On vide le set de FD pour le select */
		FD_ZERO(&fds);

		/* On y ajoute l'entrée standard */
		FD_SET(STDIN_FILENO, &fds);

		/* Ainsi que le socket avec le serveur */
		FD_SET(sock, &fds);

		/* Et on attend de recevoir quelque chose */
		SYS(select(sock + 1, &fds, NULL, NULL, NULL));

		/* Si on recoit quelque chose de l'entrée standard */
		if (FD_ISSET(STDIN_FILENO, &fds))
		{
			/* On lit BUF_SIZE - 1 pour pouvoir rajouter \0 à la fin */
			SYS(n = read(STDIN_FILENO, buffer, BUF_SIZE - 1));
			buffer[n - 1] = '\0';

			/* Si c'est à mon tour d'attaquer */
			if (my_turn_attack)
			{
				Message m;
				char *endptr;

				/* Format attaque : X;Y;Z (5 caractères minimum) */
				if (n < 5)
				{
					printf("Entrez une attaque valide ! %s\n", ATT_FORMAT);
					continue;
				}

				m.msgAttack.type = MSG_ATTACK;
				m.msgAttack.source = my_id;

				/* On récupère le joueur ciblé par l'attaque */
				m.msgAttack.target = strtol(strtok(buffer, ";"), &endptr, 10);
				if (*endptr != '\0' || my_id == m.msgAttack.target || m.msgAttack.target < 0 || m.msgAttack.target >= board.length)
				{
					printf("La cible doit être valide ! %s\n", ATT_FORMAT);
					continue;
				}

				/* On vérifie si le joueur que l'on attaque n'a pas déjà perdu */
				if (!connected[m.msgAttack.target] || get_num_hiddens(&(board.decks[m.msgAttack.target])) == 0)
				{
					printf("Ce joueur a déjà perdu, recommencez votre attaque ! %s\n", ATT_FORMAT);
					continue;
				}

				/* On récupère la position où l'attaque à lieu */
				m.msgAttack.pos = strtol(strtok(NULL, ";"), &endptr, 10);
				m.msgAttack.pos--;
				if (*endptr != '\0' || m.msgAttack.pos < 0 || m.msgAttack.pos >= board.decks[m.msgAttack.target].length)
				{
					printf("La position doit être valide ! %s\n", ATT_FORMAT);
					continue;
				}

				/* On récupère le numéro de la pièce pour l'attaque */
				m.msgAttack.num_tok = strtol(strtok(NULL, ";"), &endptr, 10);
				if (*endptr != '\0' || m.msgAttack.num_tok < 0 || m.msgAttack.num_tok > 11)
				{
					printf("La pièce doit être valide ! %s\n", ATT_FORMAT);
					continue;
				}

				/* On envoie ensuite l'attaque au serveur si tout s'est bien passé */
				SYS(write(sock, &m, sizeof(m)));
				my_turn_attack = FALSE; /* Fin de l'attaque */
			}
			/* Si c'est à mon tour de confirmer une attaque */
			else if (my_turn_confirm)
			{
				Message m;
				m.msgString.type = MSG_OKKO;

				/* On vérifie que le joueur ait répondu OK ou KO */
				if (strcmp(buffer, CONFIRM_OK) && strcmp(buffer, CONFIRM_KO))
				{
					printf("Entrez %s ou %s !\n", CONFIRM_OK, CONFIRM_KO);
					continue;
				}

				strcpy(m.msgString.msg, buffer);
				SYS(write(sock, &m, sizeof(m)));
				my_turn_confirm = FALSE; /* Fin de la confirmation */
			}
			/* Si c'est à mon tour de donner la position de la pièce courante que j'ai reçue */
			else if (my_turn_pos)
			{
				Message m;
				m.msgPos.type = MSG_POS;
				m.msgPos.t = current;
				m.msgPos.pos = atoi(buffer);
				m.msgPos.pos--;

				/* Vérifions que le joueur n'écrive pas des âneries */
				if (m.msgPos.pos < 0 || m.msgPos.pos > board.decks[my_id].length)
				{
					printf("Position non valide, recommencez !\n");
					continue;
				}

				SYS(write(sock, &m, sizeof(m)));
				my_turn_pos = FALSE; /* Fin de la position */
			}
			/* Le joueur n'a rien a dire pour l'instant nous ne tenons donc pas compte de ce qu'il écrit */
			else
			{
				printf("Attendez votre tour pour jouer !\n");
			}
		}
		/* Si l'on reçoit quelque chose du serveur sur le socket */
		else if (FD_ISSET(sock, &fds))
		{
			Message m =
			{ 0 };

			SYS(n = read(sock, &m, sizeof(m)));

			if (!n)
			{
				/* Le serveur à coupé la connexion */
				printf("Le serveur a coupé la connexion.\n");
				SYS(close(sock));
				exit(EXIT_SUCCESS);
			}

			/* On traite différement chaque type de message */
			switch (m.type)
			{
			case MSG_POS: /* Message demandant la position de la pièce courante */
				printf("Quelle est la position de la pièce courante (%d%c)\n", current.num, current.color);
				my_turn_pos = TRUE;
				break;
			case MSG_STARTED: /* Message pour indiquer que la partie a déjà commencé */
			case MSG_STRING: /* Message contenant du texte à afficher */
				printf("%s\n", m.msgString.msg);
				break;
			case MSG_DECK: /* Message contenant les pièces du joueur */
				system("clear"); /* Saut de page avant affichage*/
				printf("Vos pièces :\n");
				print_deck(&(m.msgDeck.deck));
				printf("\n");
				break;
			case MSG_BOARD: /* Le plateau de jeu avec les pièces cachées */
				printf("Le plateau de jeu :\n");
				print_board(&(m.msgBoard.board));
				board = m.msgBoard.board;
				break;
			case MSG_TOKEN: /* La pièce courante indiquant le début de l'attaque */
				current = m.msgOneToken.t;
				if (current.num >= 0)
				{
					printf("Vous avez la pièce %d%c\n", current.num, current.color);
				}
				else
				{
					/* Si la pièce vaut -1 c'est que la pioche est vide */
					printf("Il n'y a plus de pièces dans la pioche.\n");
				}
				printf("Veuillez attaquer un joueur : %s\n", ATT_FORMAT);
				my_turn_attack = TRUE;
				break;
			case MSG_ATTACK: /* Message indiquant l'attaque d'un joueur envers un autre */
				printf("%s attaque %s à la position %d avec la pièce %d\n", board.id[m.msgAttack.source], board.id[m.msgAttack.target],
						m.msgAttack.pos + 1, m.msgAttack.num_tok);

				/* Si l'attaque concerne ce joueur, il faut y répondre */
				if (m.msgAttack.target == my_id)
				{
					printf("%s réussi-t-il son attaque contre vous? (OK|KO)\n", board.id[m.msgAttack.source]);
					my_turn_confirm = TRUE;
				}
				break;
			case MSG_INIT_DECK: /* Message contenant les pièces qu'il faut trié puis renvoyer au serveur */
				/* On trie les pièces puis on les renvoies au serveur */
				qsort(m.msgDeck.deck.tokens, m.msgDeck.deck.length, sizeof(Token), (cmpfct) compare);
				SYS(write(sock, &m, sizeof(m)));
				printf("\n");
				break;
			case MSG_LOGOUT:
				printf("Le joueur %d-%s s'est déconnecté\n", m.msgLogout.player_id, board.id[m.msgLogout.player_id]);
				connected[m.msgLogout.player_id] = FALSE;
				break;
			}

			/* Si le message est d'un de ces types, on quitte le client */
			if (m.type == MSG_STARTED)
			{
				exit(EXIT_FAILURE);
			}
		}
	}
}

/* Méthode pour affiche un ensemble de pièces */
void print_deck(const Deck *deck)
{
	int i;
	for (i = 0; i < deck->length; i++)
	{
		if (deck->tokens[i].num == HIDDEN)
		{
			/* Pièce cachée */
			printf("  *%c", deck->tokens[i].color);
		}
		else
		{
			/* Pièce retournée */
			printf("%3d%c", deck->tokens[i].num, deck->tokens[i].color);
		}

		printf("%s", (i < deck->length - 1 ? "," : ""));
	}
}

/* Méthode pour connaître le nombre de pièces cachées dans une série */
int get_num_hiddens(const Deck *deck)
{
	int i;
	int h = 0;
	for (i = 0; i < deck->length; i++)
	{
		if (deck->tokens[i].hidden)
		{
			h++;
		}
	}
	return h;
}

/* Méthode pour afficher tout le tableau de jeu */
void print_board(const GameBoard *board)
{
	int i;
	for (i = 0; i < board->length; i++)
	{
		if (!strcmp(my_login, board->id[i]))
		{
			/* si mon login est le même que celui du tableau je conserve mon id */
			my_id = i;
			printf("  %s :\t", board->id[i]);
		}
		else
		{
			printf("%d-%s :\t", i, board->id[i]);
		}

		print_deck(&(board->decks[i]));

		/* On affiche les joueurs qui ont perdu (toutes les pièces sont dévoilées) */
		if (get_num_hiddens(&(board->decks[i])) == 0)
		{
			printf("\t[Perdu]");
		}

		printf("\n");
	}
}

/* Fonction de comparaison pour la fonction qsort */
int compare(Token *a, Token *b)
{
	if (a->num == b->num)
	{
		if (a->color == b->color)
		{
			return 0;
		}
		/* Noir avant le blanc */
		else if (a->color == BLACK)
		{
			return -1;
		}
		else
		{
			return 1;
		}
	}
	return a->num - b->num;
}

/* Connexion au serveur */
SOCKET init_connection(const char *address, const int port)
{
	SOCKET sock;
	struct hostent *hostinfo;
	SOCKADDR_IN sin =
	{ 0 };

	SYS(sock = socket(AF_INET, SOCK_STREAM, 0));

	hostinfo = gethostbyname(address);
	if (hostinfo == NULL)
	{
		fprintf(stderr, "Hôte inconnu %s.\n", address);
		exit(EXIT_FAILURE);
	}

	sin.sin_addr = *(IN_ADDR *) hostinfo->h_addr;
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	if (connect(sock, (SOCKADDR *) &sin, sizeof(SOCKADDR)) == -1)
	{
		if (errno == ECONNREFUSED)
		{
			fprintf(stderr, "Connexion refusée car pas de serveur lancé sur ce port.\n");
		}
	}

	return sock;
}

/* Point d'entrée du client */
int main(int argc, const char ** argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "Usage : %s adresse pseudo port\n", argv[0]);
		return EXIT_FAILURE;
	}

	app(argv[1], argv[2], atoi(argv[3]));

	return EXIT_SUCCESS;
}
